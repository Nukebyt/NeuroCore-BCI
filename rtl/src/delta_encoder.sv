// Delta (UP/DOWN) spike encoder, one channel (ROADMAP §7.1).
// Bit-exact with ml/models/snn/delta.py; tested by rtl/tb/test_delta_encoder.py.
// On each valid_in: UP if x - ref >= THRESH (ref += THRESH), DOWN if ref - x >= THRESH
// (ref -= THRESH). Spikes appear with valid_out one clock later.
module delta_encoder #(
    parameter int W      = 16,  // signed input width
    parameter int THRESH = 64   // must be < 2**(W-1)
) (
    input  logic                clk,
    input  logic                rst_n,      // synchronous, active low
    input  logic                valid_in,
    input  logic signed [W-1:0] x_in,
    output logic                valid_out,
    output logic                up,
    output logic                down
);
  localparam logic signed [W:0] TH = THRESH;

  // One extra bit: ref stays within [min(x) - THRESH, max(x) + THRESH].
  logic signed [W:0] ref_q;
  logic signed [W:0] diff;

  assign diff = {x_in[W-1], x_in} - ref_q;

  always_ff @(posedge clk) begin
    if (!rst_n) begin
      ref_q     <= '0;
      valid_out <= 1'b0;
      up        <= 1'b0;
      down      <= 1'b0;
    end else begin
      valid_out <= valid_in;
      up        <= 1'b0;
      down      <= 1'b0;
      if (valid_in) begin
        if (diff >= TH) begin
          up    <= 1'b1;
          ref_q <= ref_q + TH;
        end else if (diff <= -TH) begin
          down  <= 1'b1;
          ref_q <= ref_q - TH;
        end
      end
    end
  end
endmodule
