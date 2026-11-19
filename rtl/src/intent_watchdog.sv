// Intent watchdog (ROADMAP §7.2 step 3; docs/safety.md S1 on the FPGA path).
//
// Passes the last valid intent through. If no valid_in arrives for TIMEOUT_CYCLES clocks
// (or since reset), intent_out is STOP until the next valid_in.
// Proven in rtl/formal/intent_watchdog.sby; simulated in rtl/tb/test_intent_watchdog.py.
//
// The counter saturates at TIMEOUT_CYCLES and timed_out uses >=, so an out-of-range counter
// value (e.g. a bit flip) also reads as timed out: fail safe.
module intent_watchdog #(
    parameter int INTENT_W = 4,
    parameter int TIMEOUT_CYCLES = 15_000_000,  // 300 ms at 50 MHz (DE10-Standard clock)
    parameter int CNT_W = $clog2(TIMEOUT_CYCLES + 1)
) (
    input  logic                clk,
    input  logic                rst_n,       // synchronous, active low
    input  logic                valid_in,
    input  logic [INTENT_W-1:0] intent_in,
    output logic [INTENT_W-1:0] intent_out,
    output logic                timed_out
);
  localparam logic [INTENT_W-1:0] STOP = '0;  // NIP intent 0
  localparam logic [CNT_W-1:0] LIMIT = TIMEOUT_CYCLES;

  logic [   CNT_W-1:0] idle_cnt;
  logic [INTENT_W-1:0] held;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      idle_cnt <= LIMIT;
      held     <= STOP;
    end else if (valid_in) begin
      idle_cnt <= '0;
      held     <= intent_in;
    end else if (idle_cnt < LIMIT) begin
      idle_cnt <= idle_cnt + 1'b1;
    end
  end

  assign timed_out  = (idle_cnt >= LIMIT);
  assign intent_out = timed_out ? STOP : held;

`ifdef FORMAL
  // Independent of idle_cnt: remember valid_in for the last TIMEOUT_CYCLES clocks.
  // Only meaningful with a small TIMEOUT_CYCLES (the .sby file sets 8).
  logic [TIMEOUT_CYCLES-1:0] f_hist;
  logic                      f_past_valid = 1'b0;

  always_ff @(posedge clk) begin
    f_past_valid <= 1'b1;
    f_hist       <= rst_n ? {f_hist[TIMEOUT_CYCLES-2:0], valid_in} : '0;
  end

  always_comb begin
    if (!f_past_valid) assume (!rst_n);  // start in reset
  end

  always_comb begin
    // S1: no valid input in the last TIMEOUT_CYCLES clocks (or since reset) -> STOP.
    if (f_past_valid && f_hist == '0) assert (intent_out == STOP && timed_out);
  end

  always_ff @(posedge clk) begin
    // Not over-eager: a valid input is passed through on the next clock.
    if (f_past_valid && $past(rst_n) && $past(valid_in)) begin
      assert (intent_out == $past(intent_in));
      assert (!timed_out);
    end
  end

  always_ff @(posedge clk) begin
    if (f_past_valid && rst_n && $past(rst_n)) begin
      cover (timed_out && $past(!timed_out));  // a live link times out (not via reset)
      cover (!timed_out && intent_out != STOP);  // and carries a non-STOP intent
    end
  end
`endif
endmodule
