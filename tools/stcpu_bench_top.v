// =============================================================================
//  stcpu_bench_top.v — fx68k et le GSTMCU (STE) réunis : ce que le CPU voit du faisceau.
//
//  POURQUOI. Les dates du compteur vidéo, des HBL et des VBL vues du CPU sont calées sur Hatari.
//  Ce banc fait tourner le même programme ici et dans NeoST, et compare ce qu'il a relevé en RAM.
//  Deux RTL « forts », grade D. Câblage : extern/mistery/atarist/atarist_sdram.sv:525-552 @ a00deb5b.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
// =============================================================================
module stcpu_bench_top(
    input clk32, input resb, input porb, input pwrUp, input extReset,
    input [15:0] mdin,
    output [23:1] ram_a, output we_n, output [15:0] mdout,
    output RAS0_N, output RAS1_N, output RAM_LDS, output RAM_UDS, output rom_n,
    output [23:1] cpu_a, output as_n, output rw, output [15:0] cpu_dout, output halted_n, output vsync_n
);
    wire uds_n, lds_n, vma_n, fc0, fc1, fc2, dtack_n, vpa_n, berr_n, ipl0_n, ipl1_n, ipl2_n, en1, en2;
    wire [15:0] din, dout;
    wire r0, r1, r2, r3, r4, r5, r6, rp;
    assign cpu_dout = din;
    assign rom_n = r0 & r1 & r2 & r3 & r4 & r5 & r6 & rp;

    ste_tb tb(
        .clk32(clk32), .resb(resb), .porb(porb), .BR_N(1'b1), .FC0(fc0), .FC1(fc1), .FC2(fc2),
        .AS_N(as_n), .RW(rw), .UDS_N(uds_n), .LDS_N(lds_n), .VMA_N(vma_n), .MFPINT_N(1'b1), .A(cpu_a),
        .DIN(din), .DOUT(dout), .MHZ8(), .MHZ8_EN1(en1), .MHZ8_EN2(en2), .MHZ4(), .MHZ4_EN(), .BERR_N(berr_n),
        .IPL0_N(ipl0_n), .IPL1_N(ipl1_n), .IPL2_N(ipl2_n), .DTACK_N(dtack_n), .IACK_N(),
        .ROM0_N(r0), .ROM1_N(r1), .ROM2_N(r2), .ROM3_N(r3), .ROM4_N(r4), .ROM5_N(r5), .ROM6_N(r6), .ROMP_N(rp),
        .RAM_N(), .RAS0_N(RAS0_N), .RAS1_N(RAS1_N), .CAS0L_N(), .CAS0H_N(), .CAS1L_N(), .CAS1H_N(),
        .RAM_LDS(RAM_LDS), .RAM_UDS(RAM_UDS), .REF(), .VPA_N(vpa_n), .MFPCS_N(), .SNDIR(), .SNDCS(), .N6850(), .FCS_N(),
        .RTCCS_N(), .RTCRD_N(), .RTCWR_N(), .HSYNC_N(), .VSYNC_N(vsync_n), .BLANK_N(), .SINT(),
        .MONO(), .R(), .G(), .B(), .ram_a(ram_a), .we_n(we_n), .mdout(mdout), .mdin(mdin), .turbo(1'b0), .bus_free());

    fx68k cpu(
        .clk(clk32), .HALTn(1'b1), .extReset(extReset), .pwrUp(pwrUp), .enPhi1(en1), .enPhi2(en2),
        .eRWn(rw), .ASn(as_n), .LDSn(lds_n), .UDSn(uds_n), .E(), .VMAn(vma_n),
        .FC0(fc0), .FC1(fc1), .FC2(fc2), .BGn(), .oRESETn(), .oHALTEDn(halted_n),
        .DTACKn(dtack_n), .VPAn(vpa_n), .BERRn(berr_n), .BRn(1'b1), .BGACKn(1'b1),
        .IPL0n(ipl0_n), .IPL1n(ipl1_n), .IPL2n(ipl2_n), .iEdb(dout), .oEdb(din), .eab(cpu_a));
endmodule
