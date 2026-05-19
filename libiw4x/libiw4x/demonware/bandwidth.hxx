#pragma once

namespace iw4x::demonware
{
  // Bandwidth test client.
  //
  // The engine uses bdBandwidthTestClient to measure network throughput before
  // allowing the player into matchmaking. Since we are running everything
  // locally, we just pretend the test completes instantly with zeroed bandwidth
  // values. The engine reads the zeroed values and is perfectly happy with them
  // in that it only cares that the test complete, not what the actual numbers
  // are.
  //
  namespace bandwidth
  {
    void
    init ();
  }
}
