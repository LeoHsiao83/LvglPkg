
#include "LvglUefiPort.h"

//
// MSVC references _fltused from every object that uses floating point.
//
int  _fltused = 1;

typedef union {
  double    d;
  UINT64    u;
} LVGL_DOUBLE_BITS;

//
// 2^52: every double at or above this magnitude is already an integer.
//
#define LVGL_DOUBLE_INT_LIMIT  4503599627370496.0

double
fabs (
  double  x
  )
{
  LVGL_DOUBLE_BITS  Bits;

  Bits.d  = x;
  Bits.u &= ~BIT63;
  return Bits.d;
}

double
floor (
  double  x
  )
{
  double  Result;

  if (!(fabs (x) < LVGL_DOUBLE_INT_LIMIT)) {
    return x;
  }

  Result = (double)(INT64)x;
  if (Result > x) {
    Result -= 1.0;
  }

  return Result;
}

double
ceil (
  double  x
  )
{
  double  Result;

  if (!(fabs (x) < LVGL_DOUBLE_INT_LIMIT)) {
    return x;
  }

  Result = (double)(INT64)x;
  if (Result < x) {
    Result += 1.0;
  }

  return Result;
}

//
// Negative input returns 0 instead of NaN.
//
double
sqrt (
  double  x
  )
{
  LVGL_DOUBLE_BITS  Bits;
  double            Result;
  UINTN             Index;

  if (!(x > 0.0) || (x - x != 0.0)) {
    return (x > 0.0) ? x : 0.0;
  }

  //
  // Halving the exponent gives a guess within ~6%; each Newton step squares
  // the relative error, so 5 steps reach full double precision.
  //
  Bits.d = x;
  Bits.u = (Bits.u >> 1) + 0x1FF8000000000000ULL;
  Result = Bits.d;
  for (Index = 0; Index < 5; Index++) {
    Result = 0.5 * (Result + x / Result);
  }

  return Result;
}
