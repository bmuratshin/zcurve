#if !defined(RANDOM_GENERATOR_INCLUDED)
#define RANDOM_GENERATOR_INCLUDED


#include <stdlib.h>
#include <stdint.h>
#include <math.h>

const int N = 624;		// Replaced the #defines for these with const values - DHL
const int M = 397;
const unsigned int K = 0x9908B0DFU;
const unsigned int DEFAULT_SEED = 4357;

#define hiBit(u)       ((u) & 0x80000000U)	// mask all but highest   bit of u
#define loBit(u)       ((u) & 0x00000001U)	// mask all but lowest    bit of u
#define loBits(u)      ((u) & 0x7FFFFFFFU)	// mask     the highest   bit of u
#define mixBits(u, v)  (hiBit(u)|loBits(v))	// move hi bit of u to hi bit of v

class CRandomMT
{
  uint32_t state[N + 1];	// state vector + 1 extra to not violate ANSI C
  uint32_t *next;		// next random value is computed from here
  int left;			// can *next++ this many times before reloading
  uint32_t seedValue;		// Added so that setting a seed actually maintains 
  // that seed when a ReloadMT takes place.

public:
    CRandomMT ();
    CRandomMT (uint32_t seed);
   ~CRandomMT ()
  {
  };
  void SeedMT (uint32_t seed);


  inline uint32_t RandomMT (void)
  {
    uint32_t y;

    if (--left < 0)
      return (ReloadMT ());

    y = *next++;
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9D2C5680U;
    y ^= (y << 15) & 0xEFC60000U;
    return (y ^ (y >> 18));
  }


  inline int RandomMax (int max)
  {
    return (((int) RandomMT () % max));
  }


  inline int RandomRange (int lo, int hi)
  {
    // Changed thanks to mgearman's message
    return (abs ((int) RandomMT () % (hi - lo + 1)) + lo);
  }

  inline int RollDice (int face, int number_of_dice)
  {
    int roll = 0;
    for (int loop = 0; loop < number_of_dice; loop++)
      {
	roll += (RandomRange (1, face));
      }
    return roll;
  }

  inline int HeadsOrTails ()
  {
    return ((RandomMT ()) % 2);
  }

  inline int D6 (int die_count)
  {
    return RollDice (6, die_count);
  }
  inline int D8 (int die_count)
  {
    return RollDice (8, die_count);
  }
  inline int D10 (int die_count)
  {
    return RollDice (10, die_count);
  }
  inline int D12 (int die_count)
  {
    return RollDice (12, die_count);
  }
  inline int D20 (int die_count)
  {
    return RollDice (20, die_count);
  }
  inline int D25 (int die_count)
  {
    return RollDice (25, die_count);
  }

private:
  uint32_t ReloadMT (void);

};

#endif //RANDOM_GENERATOR_INCLUDED

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
