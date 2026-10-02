/** @file
  Runtime UI scale helpers for LvglTheme.h.

  LvglLib calls LvglThemeInitFromUiScale() once at display init. Theme macros
  in LvglTheme.h route pixel and font values through these helpers so layout
  scales at draw time while LVGL renders at the physical framebuffer resolution.

  Copyright (c) 2026, MrChromebox. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef LVGL_THEME_LIB_H_
#define LVGL_THEME_LIB_H_

#include <Uefi.h>
#include <lvgl.h>

VOID
EFIAPI
LvglThemeInitFromUiScale (
  IN UINT8  UiScale
  );

UINT32
EFIAPI
LvglThemePx (
  IN UINT32  BasePx
  );

const lv_font_t *
EFIAPI
LvglThemeFontTitle (
  VOID
  );

const lv_font_t *
EFIAPI
LvglThemeFontBody (
  VOID
  );

const lv_font_t *
EFIAPI
LvglThemeFontPopup (
  VOID
  );

VOID
EFIAPI
LvglThemeApplyBodyFont (
  IN lv_obj_t  *Obj
  );

VOID
EFIAPI
LvglThemeStyleCheckbox (
  IN lv_obj_t  *Cb
  );

VOID
EFIAPI
LvglThemeStyleDropdown (
  IN lv_obj_t  *Dd
  );

VOID
EFIAPI
LvglThemeStyleDropdownList (
  IN lv_obj_t  *List
  );

VOID
EFIAPI
LvglThemeStyleTextarea (
  IN lv_obj_t  *Ta
  );

VOID
EFIAPI
LvglThemeStyleButton (
  IN lv_obj_t  *Btn
  );

UINT32
EFIAPI
LvglThemeDropdownWidth (
  IN UINTN  MaxChars
  );

UINT32
EFIAPI
LvglThemeDateTimeFieldWidth (
  IN UINTN  Digits
  );

UINT32
EFIAPI
LvglThemeImageZoom (
  VOID
  );

VOID
EFIAPI
LvglThemeApplyPopupFont (
  IN lv_obj_t  *Obj
  );

VOID
EFIAPI
LvglThemeStyleButtonLabel (
  IN lv_obj_t  *Btn,
  IN lv_obj_t  *Lbl
  );

VOID
EFIAPI
LvglThemeStyleKeyboard (
  IN lv_obj_t  *Kb
  );

VOID
EFIAPI
LvglThemeStyleCursor (
  IN lv_obj_t  *CursorImg
  );


/**
  Return the platform TrueType font at exactly Px pixels, not scaled by the
  UI scale. Fonts are created on first use and kept for the driver lifetime.

  @param[in] Px  Pixel height.

  @return The TTF font, or the closest compiled-in Montserrat font when no
          TTF is loaded or the cache is full. Never NULL.
**/
const lv_font_t *
EFIAPI
LvglThemeFontPx (
  IN UINT32  Px
  );


/**
  Load a PNG stored as the RAW section of FREEFORM file FileGuid in any FV and
  return an image source for lv_image_set_src(). The descriptor and the file
  data are kept for the driver lifetime.

  @param[in] FileGuid  FFS file name.

  @retval NULL  File not found or not a PNG; the caller draws its fallback.
**/
CONST lv_image_dsc_t *
EFIAPI
LvglThemeImageFromFv (
  IN CONST EFI_GUID  *FileGuid
  );
#endif // LVGL_THEME_LIB_H_
