// =============================================================================
//  stblitter_bench_top.sv — fx68k et stBlitter sur un même bus, pour le banc du blitter.
//
//  POURQUOI. stBlitter.sv (Jorge Cwik) ne se confronte qu'avec un vrai maître de bus en face :
//  l'arbitrage (BR/BG/BGACK) et le compteur de 64 cycles bus dépendent du 68000. On câble donc
//  les deux RTL du même auteur comme le fait MiSTery. Grade D : un modèle, pas du silicium.
//  [D] extern/mistery/atarist/atarist_sdram.sv:268-283 et 868-897 @ a00deb5b : mêmes mux.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test.
// =============================================================================
`timescale 1 ns / 1 ns

module stblitter_bench_top(
    input  logic clk, pwrUp, extReset, enPhi1, enPhi2,
    input  logic mem_dtack_n,
    input  logic [15:0] mem_data,      // lecture mémoire présentée par le pilote
    output logic as_n, rw_n, uds_n, lds_n,
    output logic [2:0] fc,
    output logic [23:1] addr,
    output logic [15:0] wdata,         // donnée écrite par le maître courant
    output logic blit_has_bus, blit_sel, blit_irq_n, halted_n
);
    blt_clks Clks;
    assign Clks.clk = clk;
    assign Clks.aRESETn = !pwrUp;
    assign Clks.sReset = pwrUp | extReset;
    assign Clks.extReset = extReset;
    assign Clks.pwrUp = pwrUp;
    assign Clks.enPhi1 = enPhi1;
    assign Clks.enPhi2 = enPhi2;
    assign Clks.anyPhi = enPhi1 | enPhi2;
    assign Clks.phi1 = 1'b0;
    assign Clks.phi2 = 1'b0;

    wire cpu_as_n, cpu_rw, cpu_uds_n, cpu_lds_n, cpu_fc0, cpu_fc1, cpu_fc2, cpu_bg_n;
    wire [23:1] cpu_a;
    wire [15:0] cpu_dout;
    wire blt_as_n, blt_ds_n, blt_rw_n, blt_dtack_n, blt_br_n, blt_bgack_n, blt_bgo_n, blt_data_oe;
    wire [23:1] blt_addr;
    wire [15:0] blt_dout;

    assign as_n  = blit_has_bus ? blt_as_n : cpu_as_n;
    assign rw_n  = blit_has_bus ? blt_rw_n : cpu_rw;
    assign uds_n = blit_has_bus ? blt_ds_n : cpu_uds_n;
    assign lds_n = blit_has_bus ? blt_ds_n : cpu_lds_n;
    assign fc    = blit_has_bus ? 3'b101 : {cpu_fc2, cpu_fc1, cpu_fc0};
    assign addr  = blit_has_bus ? blt_addr : cpu_a;
    assign wdata = blit_has_bus ? blt_dout : cpu_dout;

    wire dtack_n = mem_dtack_n & blt_dtack_n;
    wire [15:0] bus_in = blit_sel ? blt_dout : mem_data;

    fx68k cpu(
        .clk(clk), .HALTn(1'b1), .extReset(extReset), .pwrUp(pwrUp), .enPhi1(enPhi1), .enPhi2(enPhi2),
        .eRWn(cpu_rw), .ASn(cpu_as_n), .LDSn(cpu_lds_n), .UDSn(cpu_uds_n), .E(), .VMAn(),
        .FC0(cpu_fc0), .FC1(cpu_fc1), .FC2(cpu_fc2), .BGn(cpu_bg_n), .oRESETn(), .oHALTEDn(halted_n),
        .DTACKn(dtack_n), .VPAn(1'b1), .BERRn(1'b1), .BRn(blt_br_n), .BGACKn(blt_bgack_n),
        .IPL0n(1'b1), .IPL1n(1'b1), .IPL2n(1'b1), .iEdb(bus_in), .oEdb(cpu_dout), .eab(cpu_a));

    stBlitter blt(
        .Clks(Clks), .ASn(as_n), .RWn(cpu_rw), .LDSn(lds_n), .UDSn(uds_n),
        .FC0(fc[0]), .FC1(fc[1]), .FC2(fc[2]), .BERRn(1'b1), .iDTACKn(dtack_n),
        .ctrlOe(blit_has_bus), .dataOe(blt_data_oe), .oASn(blt_as_n), .oDSn(blt_ds_n), .oRWn(blt_rw_n),
        .oDTACKn(blt_dtack_n), .selected(blit_sel),
        .iBRn(1'b1), .BGIn(cpu_bg_n), .iBGACKn(1'b1), .oBRn(blt_br_n), .oBGACKn(blt_bgack_n),
        .INTn(blit_irq_n), .BGOn(blt_bgo_n), .dmaInput(mem_data),
        .iABUS(addr), .oABUS(blt_addr), .iDBUS(cpu_dout), .oDBUS(blt_dout));
endmodule
