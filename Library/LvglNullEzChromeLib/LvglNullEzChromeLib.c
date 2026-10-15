/** @file
  Default LvglEzChromeLib instance: no EZ page.

  Copyright (c) 2026, LeoHsiao83. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/LvglEzChromeLib.h>

/**
  Report whether this platform provides an EZ page.

  @retval FALSE  Always; this instance has no EZ page.
**/
BOOLEAN
EFIAPI
LvglEzChromePresent (
  VOID
  )
{
  return FALSE;
}

/**
  Report whether Setup should open on the EZ page.

  @retval FALSE  Always.
**/
BOOLEAN
EFIAPI
LvglEzChromePreferEz (
  VOID
  )
{
  return FALSE;
}

/**
  Build the EZ page. Nothing to build.
**/
VOID
EFIAPI
LvglEzChromeBuild (
  VOID
  )
{
}

/**
  Tear down the EZ page. Nothing to tear down.
**/
VOID
EFIAPI
LvglEzChromeTeardown (
  VOID
  )
{
}

/**
  Report a leave request. This instance never asks to leave.

  @retval FALSE  Always.
**/
BOOLEAN
EFIAPI
LvglEzChromeLeaveRequested (
  VOID
  )
{
  return FALSE;
}

/**
  Reset failed after a leave request. Nothing to show.
**/
VOID
EFIAPI
LvglEzChromeResetFailed (
  VOID
  )
{
}
