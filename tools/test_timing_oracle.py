#!/usr/bin/env python3
"""Vérifie que le comparateur temporel refuse les faux verts et les mutations."""
import contextlib
import copy
import io
import json
from pathlib import Path
import re
import tempfile
import unittest

import run_timing_oracle as timing
import trace_diff as td
from make_timing_probe import build
from run_mfp_iack_probe import check_output as check_iack_probe


class HardwareProbeProtocol(unittest.TestCase):
    def test_completion_requires_all_counts_and_single_record(self):
        valid = bytes.fromhex("004800480048beef")
        self.assertEqual(check_iack_probe(valid, "", 72, True)["complete_records"], 1)
        for mutant in (b"", bytes.fromhex("004800470048beef"), valid * 2):
            with self.subTest(mutant=mutant), self.assertRaises(ValueError):
                check_iack_probe(mutant, "", 72, True)

    def test_unsupported_machine_must_really_execute_skip_path(self):
        skipped = bytes.fromhex("4941434b5a1f0000")
        self.assertEqual(check_iack_probe(skipped, "", 72, False)["skipped_records"], 1)
        # Un boot qui n'a jamais lancé AUTO ne doit pas compter comme un refus réussi.
        with self.assertRaises(ValueError):
            check_iack_probe(b"\x00" * 1024, "", 72, False)

    def test_halt_cannot_pass_with_old_completion_record(self):
        with self.assertRaises(ValueError):
            check_iack_probe(bytes.fromhex("004800480048beef"), "[cpu] 68000 halted", 72, True)


class TimingComparison(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.all_reference = json.loads(timing.REFERENCE.read_text())["cases"]
        cls.reference = {c: r for c, r in cls.all_reference.items() if "first_irq" in r}

    def check(self, case, measurement):
        with contextlib.redirect_stdout(io.StringIO()):
            return timing.compare(case, measurement, self.reference[case])

    def test_references_match_themselves(self):
        for case, reference in self.reference.items():
            with self.subTest(case=case):
                self.assertTrue(self.check(case, reference))

    def test_missing_interrupt_with_unchanged_dominant_period(self):
        for case, reference in self.reference.items():
            with self.subTest(case=case):
                mutant = copy.deepcopy(reference)
                times = mutant["cycles"]
                del times[100]
                times.append(times[-1] + times[-1] - times[-2])
                self.assertFalse(self.check(case, mutant))

    def test_slow_drift_with_unchanged_dominant_period(self):
        for case, reference in self.reference.items():
            with self.subTest(case=case):
                mutant = copy.deepcopy(reference)
                limit = timing.LIMITS[case][0]
                mutant["cycles"] = [t + (i // 64) * limit for i, t in enumerate(mutant["cycles"])]
                self.assertFalse(self.check(case, mutant))

    def test_delayed_first_irq(self):
        for case, reference in self.reference.items():
            mutant = copy.deepcopy(reference)
            mutant["first_irq"] += timing.LIMITS[case][1] + 1
            self.assertFalse(self.check(case, mutant))

    def test_changed_probe_and_short_reference(self):
        mutant = copy.deepcopy(self.reference["timer_a"])
        mutant["probe_sha256"] = "wrong"
        with self.assertRaises(ValueError):
            self.check("timer_a", mutant)
        mutant = copy.deepcopy(self.reference["timer_a"])
        mutant["cycles"].pop()
        with self.assertRaises(ValueError):
            self.check("timer_a", mutant)

    def test_reset_clock_bias_is_detected(self):
        # Régression observée : compteur brut Moira (+40 au reset), puis
        # changement d'origine au premier IACK. Elle ne doit plus être tolérée.
        mutant = copy.deepcopy(self.reference["timer_a"])
        mutant["first_irq"] -= 40
        self.assertFalse(self.check("timer_a", mutant))

    def test_transfer_mutations_and_strict_blitter(self):
        for case in (c for c in self.all_reference if c.startswith(("fdc_", "blitter_"))):
            reference = self.all_reference[case]
            baseline = copy.deepcopy(reference)
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(timing.compare(case, baseline, reference))
                self.assertTrue(timing.compare(case, reference, reference, strict=True))
                if case == "blitter_shared":
                    legacy = copy.deepcopy(reference)
                    for field, delta in {"durations": 60, "irq_durations": 42, "polls": 3}.items():
                        legacy[field] = [t + delta for t in legacy[field]]
                    self.assertFalse(timing.compare(case, legacy, reference))
                    self.assertFalse(timing.compare(case, legacy, reference, strict=True))
                for field in (("cycles", "polls") if case.startswith("fdc_") else ("durations", "irq_durations", "polls")):
                    mutant = copy.deepcopy(baseline)
                    if field == "cycles":
                        # DMA sans stall de 32 cycles : chaque rafale avance trop tôt.
                        mutant[field] = [t - 32*i for i, t in enumerate(mutant[field])]
                    else:
                        mutant[field][12] += 8
                    self.assertFalse(timing.compare(case, mutant, reference))
                if case.startswith("fdc_"):
                    mutant = copy.deepcopy(baseline)
                    mutant["completion"] += timing.LIMITS[case][1] + 1
                    self.assertFalse(timing.compare(case, mutant, reference))

    def test_div_order_is_exact(self):
        # Préfetch de DIVU/DIVS avant les cycles internes : quatre cycles de moins
        # sur une seule itération doivent suffire à faire échouer la sonde.
        reference = self.all_reference["cpu_div_ram"]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(timing.compare("cpu_div_ram", copy.deepcopy(reference), reference))
            for mark in ("mark_divu", "mark_divs"):
                mutant = copy.deepcopy(reference)
                mutant[mark][3] -= 4
                self.assertFalse(timing.compare("cpu_div_ram", mutant, reference))

    def test_stx_read_time_is_honoured(self):
        # ReadTime ignoré : le secteur repasse à 32 µs par octet (4 096 cycles par
        # rafale au lieu de ~5 000) ; l'écart cumulé dépasse vite un tour de scrutation.
        reference = self.all_reference["fdc_read_stx"]
        mutant = copy.deepcopy(reference)
        mutant["cycles"] = [t * 4096 // 5000 for t in mutant["cycles"]]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(timing.compare("fdc_read_stx", mutant, reference))

    def test_fdc_write_tolerates_one_poll_only(self):
        reference = self.all_reference["fdc_write"]
        with contextlib.redirect_stdout(io.StringIO()):
            mutant = copy.deepcopy(reference); mutant["polls"][5] += 1
            self.assertTrue(timing.compare("fdc_write", mutant, reference))
            mutant["polls"][5] += 1
            self.assertFalse(timing.compare("fdc_write", mutant, reference))

    def test_transfer_trace_integrity(self):
        for case in ("fdc_read", "blitter_shared"):
            data, pcs = build(case)
            def line(name, clock):
                return f"cyc={clock} {pcs[name]:06X}: nop\n"
            if case == "fdc_read":
                body = line("arm", 100)
                body += "".join(line("poll", 996+i*4128) + line("handler", 1000+i*4128) for i in range(32))
                body += line("irq", 130000)
            else:
                body = "".join(line("arm", 100+i*5000) + line("poll", 104+i*5000) + line("irq", 1100+i*5000)
                               + line("handler", 1200+i*5000) for i in range(72))
            body += line("success", 500000)
            with tempfile.TemporaryDirectory() as directory:
                trace = Path(directory) / "probe.trace"
                trace.write_text(body)
                timing.read_transfer_trace(trace, pcs, data, case)
                for invalid in (body.replace(line("success", 500000), ""),
                                body + line("failure", 500004),
                                re.sub(r"cyc=\d+ ", "", body),
                                body + line("irq", 500008)):
                    trace.write_text(invalid)
                    with self.assertRaises(ValueError):
                        timing.read_transfer_trace(trace, pcs, data, case)
    def test_absent_pc_fails_periods(self):
        trace = [td.Insn(0xFA0000, "", 0), td.Insn(0xFA0000, "", 100)]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(td.report_periods(trace, trace, ["FA0010"], False), 1)

    def test_trace_requires_clock_samples_and_single_arming(self):
        data, pcs = build("timer_a")
        def line(pc, clock):
            return f"cyc={clock} {pc:06X}: nop\n"
        arm = line(pcs["arm"], 100)
        body = "".join(line(pcs["handler"], 1000 + i * 2508)
                       for i in range(timing.WARMUP + timing.SAMPLES))
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "probe.trace"
            trace.write_text(arm + body)
            self.assertEqual(len(timing.read_trace(trace, pcs, data)["cycles"]), timing.SAMPLES)
            for invalid in (arm + body.splitlines(keepends=True)[0],
                            arm + arm + body, arm, body,
                            re.sub(r"cyc=\d+ ", "", arm + body),
                            (arm + body).replace("cyc=", "missing="),
                            arm + body + line(pcs["handler"], 0)):
                trace.write_text(invalid)
                with self.assertRaises((ValueError, SystemExit)):
                    timing.read_trace(trace, pcs, data)


class MegaSteBusComparison(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import run_megaste_bus_oracle as bus
        cls.bus = bus
        cls.reference = json.loads(bus.REFERENCE.read_text())["cases"]

    def check(self, case, actual):
        with contextlib.redirect_stdout(io.StringIO()):
            return self.bus.compare(case, actual, self.reference[case])

    def test_exact_reference_and_probe_identity(self):
        self.assertEqual(set(self.reference), set(self.bus.CASES))
        for case, ref in self.reference.items():
            with self.subTest(case=case):
                self.assertTrue(self.check(case, ref))
                data, _ = self.bus.build(case)
                import hashlib
                self.assertEqual(ref["probe_sha256"], hashlib.sha256(data).hexdigest())

    def test_one_cycle_and_clock_domain_errors_rejected(self):
        for case, ref in self.reference.items():
            for factor, offset in ((1, 1), (1, -1), (2, 0)):
                with self.subTest(case=case, factor=factor, offset=offset):
                    actual = copy.deepcopy(ref)
                    actual["bus_cycles"] = [v*factor+offset for v in ref["bus_cycles"]]
                    self.assertFalse(self.check(case, actual))

    def test_missing_sample_rejected(self):
        for case, ref in self.reference.items():
            actual = copy.deepcopy(ref)
            actual["bus_cycles"].pop()
            with self.assertRaises(ValueError):
                self.check(case, actual)

    def test_changed_program_rejected(self):
        case = next(iter(self.reference))
        actual = copy.deepcopy(self.reference[case])
        actual["probe_sha256"] = "wrong"
        with self.assertRaises(ValueError):
            self.check(case, actual)


class MegaSteTransferChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import run_megaste_blitter_checks as checks
        cls.checks = checks

    def valid(self, case, emulator="neost"):
        cfg = self.checks.CASES[case]
        factor = 2 if emulator == "hatari" and cfg["mhz"] == 16 else 1
        bound = self.checks.minimum_bus_cycles(cfg) * factor
        return {"durations": [bound] * 64, "irq_durations": [bound] * 64}

    def validate(self, case, value, emulator="neost"):
        with contextlib.redirect_stdout(io.StringIO()):
            return self.checks.validate(case, value, emulator)

    def test_lower_bounds_keep_blitter_at_bus_frequency(self):
        for case, cfg in self.checks.CASES.items():
            with self.subTest(case=case):
                bound = self.checks.minimum_bus_cycles(cfg)
                self.assertEqual(bound, 1036 if cfg["hog"] else 1840 if cfg["mhz"] == 8 else 1456)
                self.assertTrue(self.validate(case, self.valid(case)))
                self.assertTrue(self.validate(case, self.valid(case, "hatari"), "hatari"))

    def test_one_impossible_completion_or_irq_rejected(self):
        for case in self.checks.CASES:
            for field in ("durations", "irq_durations"):
                value = self.valid(case)
                value[field][35] -= 1
                self.assertFalse(self.validate(case, value))
            if self.checks.CASES[case]["mhz"] == 16:
                # Raw CPU 16 MHz interprété à tort comme des cycles bus 8 MHz.
                self.assertFalse(self.validate(case, self.valid(case), "hatari"))

    def test_missing_sample_rejected(self):
        case = next(iter(self.checks.CASES))
        value = self.valid(case)
        value["irq_durations"].pop()
        with self.assertRaises(ValueError):
            self.validate(case, value)

    def test_extra_or_early_irq_never_filtered(self):
        case = next(iter(self.checks.CASES))
        data, pcs = self.checks.build(case)
        line = lambda label, t: f"cyc={t} {pcs[label]:08X}: nop\n"
        events = [("arm", 1000+i*4000) for i in range(72)]
        events += [("poll", 1100+i*4000) for i in range(72)]
        events += [("irq", 3000+i*4000) for i in range(72)]
        events += [("handler", 3100+i*4000) for i in range(72)]
        events += [("success", 300000)]
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "probe.trace"
            for extra in ([], [("irq", 1200)], [("irq", 999)]):
                current = events if not extra else events + extra
                trace.write_text("".join(line(label, t) for label, t in sorted(current, key=lambda e: e[1])))
                if not extra:
                    timing.read_transfer_trace(trace, pcs, data, case)
                else:
                    with self.assertRaises(ValueError):
                        timing.read_transfer_trace(trace, pcs, data, case)
            early = [(label, 999 if label == "irq" and t == 3000 else t) for label, t in events]
            trace.write_text("".join(line(label, t) for label, t in sorted(early, key=lambda e: e[1])))
            with self.assertRaises(ValueError):
                timing.read_transfer_trace(trace, pcs, data, case)

    def test_irq_while_busy_rejected_even_with_valid_counts_and_durations(self):
        # Une trace peut respecter les nombres et les bornes tout en prenant
        # une IRQ de la mauvaise copie. Le verdict invité prime sur ces chiffres.
        case = next(iter(self.checks.CASES))
        data, pcs = self.checks.build(case)
        events = [(label, 1000+i*4000+offset) for i in range(72)
                  for label, offset in (("arm", 0), ("poll", 100),
                                        ("irq", 2000), ("handler", 2100))]
        events.append(("success", 300000))
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / "probe.trace"
            for copy_index in (0, 43):  # échauffement et mesure tardive
                current = events + [("irq_while_busy", 3010+copy_index*4000)]
                trace.write_text("".join(f"cyc={t} {pcs[label]:08X}: nop\n"
                                         for label, t in sorted(current, key=lambda e: e[1])))
                with self.subTest(copy_index=copy_index):
                    with self.assertRaisesRegex(ValueError, "IRQ reçue.*BUSY"):
                        timing.read_transfer_trace(trace, pcs, data, case)

    def test_iack_idle_keeps_cpu_clock_domain(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "irq.log"
            for mhz, cycles in ((8, 16), (16, 28)):
                log.write_text(f"[EXC] lvl=6 t0=0 idle+PClo=18 iack={cycles} SR/PChi=16 pc=fa0000\n" * 72)
                self.assertEqual(self.checks.read_iack_cycles(log, {"mhz": mhz}), [cycles]*72)

    def test_iack_missing_or_wrong_domain_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "irq.log"
            good = "[EXC] lvl=6 t0=0 idle+PClo=18 iack=28 SR/PChi=16 pc=fa0000\n"
            for bad in (good*71, good*73, good*71+good.replace("iack=28", "iack=32"),
                        good.replace("iack=28", "iack=16")*72):
                log.write_text(bad)
                with self.assertRaisesRegex(ValueError, "72 IACK de 28 cycles CPU"):
                    self.checks.read_iack_cycles(log, {"mhz": 16})


if __name__ == "__main__":
    unittest.main()
