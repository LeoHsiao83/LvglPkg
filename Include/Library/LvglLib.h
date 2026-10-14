/** @file
  LvglLib class with APIs from the openssl project

  Copyright (c) 2024, Yang Gang. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __LVGL_LIB_H__
#define __LVGL_LIB_H__

#if defined(_MSC_VER)
#pragma warning(disable: 4244) // workaround for misc/lv_color.h(355), remove after lvgl update
#endif

#include <lvgl.h>

//
// Custom LVGL key codes for EFI function keys F1-F12.
// EFI SCAN_F1..F12 (0x000B-0x0016) overlap with LVGL reserved key values,
// so we remap them to a safe range above 0x0100.
//
#define LV_KEY_F1   0x0101U
#define LV_KEY_F2   0x0102U
#define LV_KEY_F3   0x0103U
#define LV_KEY_F4   0x0104U
#define LV_KEY_F5   0x0105U
#define LV_KEY_F6   0x0106U
#define LV_KEY_F7   0x0107U
#define LV_KEY_F8   0x0108U
#define LV_KEY_F9   0x0109U
#define LV_KEY_F10  0x010AU
#define LV_KEY_F11  0x010BU
#define LV_KEY_F12  0x010CU

//
// PageUp / PageDown when PcdLvglPageKeysScroll is TRUE (otherwise the keypad
// reports them as LV_KEY_PREV / LV_KEY_NEXT).
//
#define LV_KEY_PAGE_UP    0x010DU
#define LV_KEY_PAGE_DOWN  0x010EU

typedef
VOID
(EFIAPI *EFI_LVGL_APP_FUNCTION)(
  VOID
  );

EFI_STATUS
EFIAPI
UefiLvglInit (
  VOID
  );

EFI_STATUS
EFIAPI
UefiLvglDeinit (
  VOID
  );

EFI_STATUS
EFIAPI
UefiLvglAppRegister (
  IN EFI_LVGL_APP_FUNCTION AppRegister
  );

/**
  Drain the EFI keyboard buffer and reset the LVGL keypad indev state so that
  no pending key-press leaks into the next event loop.

  @return  The last key drained; ScanCode and UnicodeChar are 0 if none.
**/
EFI_INPUT_KEY
lv_uefi_keypad_drain (
  void
  );

/**
  Drop Key in the keypad read until it has been quiet for more than QuietMs;
  the first read after that is passed through. A zero Key cancels.
**/
void
lv_uefi_keypad_ignore_key (
  EFI_INPUT_KEY  Key,
  UINT32         QuietMs
  );

/**
  Report whether a usable pointer (mouse) is currently present. Returns TRUE
  only after genuine pointer motion or a click has been observed, so callers
  can suppress mouse-only UI (such as the on-screen keyboard) on keyboard-only
  machines.
**/
bool
lv_uefi_pointer_is_present (
  void
  );

#endif