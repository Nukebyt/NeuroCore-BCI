# NeuroDrive safety rules

Three rules, kept because they protect the person wearing the electrodes or stop the car. Everything else is left to judgment.

1. **Battery-only while electrodes are on a person.** The acquisition board runs from its own battery. If it is connected to a laptop over USB, the laptop is unplugged from its charger, or the link goes through a USB isolator. No mains-powered equipment connected to the acquisition circuit during a session.
2. **Series resistor on every electrode input.** Never removed or bypassed to "improve signal".
3. **Car link watchdog and STOP stay in.**
   - **S1:** every adapter stops if no valid NIP frame arrives for 300 ms (`NIP_LINK_TIMEOUT_MS`).
   - **S2:** a STOP intent or the `latched_stop` flag brakes immediately.

   Changes to `firmware/*/safety/` or the stop handling keep tests that prove S1 and S2 still hold.

Not a medical device; no medical claims.
