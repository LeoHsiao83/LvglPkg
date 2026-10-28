/** @file
  Platform-swappable EZ page library class.

  A platform may supply a second, simpler Setup page (the EZ page) next to
  the Advanced pages drawn through LvglChromeLib. The display engine calls
  this class only when PcdLvglEzChromeEnable is TRUE.

  Copyright (c) 2026, LeoHsiao83. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef LVGL_EZ_CHROME_LIB_H_
#define LVGL_EZ_CHROME_LIB_H_

#include <Uefi.h>

/**
  Report whether this platform provides an EZ page.

  @retval TRUE   An EZ page is available.
  @retval FALSE  No EZ page; the engine shows the Advanced pages only.
**/
BOOLEAN
EFIAPI
LvglEzChromePresent (
  VOID
  );

/**
  Report whether Setup should open on the EZ page.

  @retval TRUE   Open on the EZ page.
  @retval FALSE  Open on the Advanced pages.
**/
BOOLEAN
EFIAPI
LvglEzChromePreferEz (
  VOID
  );

/**
  Build the EZ page on the active LVGL screen.
**/
VOID
EFIAPI
LvglEzChromeBuild (
  VOID
  );

/**
  Delete the objects and timers created by LvglEzChromeBuild.
**/
VOID
EFIAPI
LvglEzChromeTeardown (
  VOID
  );

/**
  Report (once) that the EZ page wrote BootNext and Setup should end with a
  cold reset. Polled by the engine while the EZ page is shown.

  @retval TRUE   The engine calls ResetSystem (EfiResetCold); nothing is
                 submitted.
  @retval FALSE  Stay.
**/
BOOLEAN
EFIAPI
LvglEzChromeLeaveRequested (
  VOID
  );

/**
  Report (once) that the EZ page asked for the form's SUBMIT hot key path
  (for example a save button). Polled by the engine while the EZ page is
  shown and no popup is open; the engine then does exactly what that hot
  key does on the EZ page when PcdLvglSubmitHotKeyExits is TRUE (confirm
  popup first). The chrome saves nothing itself.

  @retval TRUE   Run the SUBMIT hot key path.
  @retval FALSE  Nothing asked.
**/
BOOLEAN
EFIAPI
LvglEzChromeSaveRequested (
  VOID
  );

/**
  ResetSystem returned after a leave request. The EZ page shows that the
  reset failed; BootNext stays as written.
**/
VOID
EFIAPI
LvglEzChromeResetFailed (
  VOID
  );

#endif // LVGL_EZ_CHROME_LIB_H_
