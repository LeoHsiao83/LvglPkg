/** @file
  Text popup interception -- turns bordered dialogs written to the graphics
  console by code outside the display engine into LVGL windows.

  Copyright (c) 2026, LeoHsiao83. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef LVGL_TEXT_POPUP_H_
#define LVGL_TEXT_POPUP_H_

#include <Uefi.h>

#define TP_CELL_WIDE_TAIL  0x01

typedef struct {
  CHAR16    Ch;
  UINT8     Attr;
  UINT8     Flags;
} TP_CELL;

typedef struct {
  UINTN      Cols;
  UINTN      Rows;
  TP_CELL    *Cells;
} TP_GRID;

typedef struct {
  UINTN    Top;
  UINTN    Left;
  UINTN    Bottom;
  UINTN    Right;
} TP_BOX;

#define TP_ROW_MAX   40
#define TP_TEXT_MAX  256

typedef enum {
  TpRowTitle,
  TpRowBody,
  TpRowFooter
} TP_ROW_KIND;

typedef struct {
  TP_BOX     Box;
  UINTN      GridCols;
  BOOLEAN    Centered;
  UINTN      RowCount;
  struct {
    CHAR8      Text[TP_TEXT_MAX];
    UINT8      Kind;
    BOOLEAN    Highlight;
  } Row[TP_ROW_MAX];
} TP_SCENE;

//
// Set while LVGL is drawing; hooks must not touch LVGL then.
//
extern BOOLEAN  gLvglTpBusy;

/**
  Find the closed box whose bottom-right corner is at (Row, Col).

  @retval TRUE   A box of at least 3x3 cells was found.
**/
BOOLEAN
TpFindClosedBox (
  IN  CONST TP_GRID  *Grid,
  IN  UINTN          Row,
  IN  UINTN          Col,
  OUT TP_BOX         *Box
  );

/**
  Describe the contents of a closed box as title / body / footer rows.
**/
VOID
TpBuildScene (
  IN  CONST TP_GRID  *Grid,
  IN  CONST TP_BOX   *Box,
  OUT TP_SCENE       *Scene
  );

//
// Session control, called by the display engine.
//
VOID
LvglTpInit (
  VOID
  );

VOID
LvglTpUnload (
  VOID
  );

VOID
LvglTpFormEnter (
  VOID
  );

VOID
LvglTpFormLeave (
  VOID
  );

VOID
LvglTpEnd (
  VOID
  );

VOID
LvglTpIgnoreClears (
  IN BOOLEAN  Ignore
  );

VOID
LvglTpFrame (
  VOID
  );

//
// Rendering, implemented in LvglFormRenderer.c.
//
VOID
LvglTpUiShow (
  IN CONST TP_SCENE  *Scene
  );

VOID
LvglTpUiDismiss (
  IN BOOLEAN  Refresh
  );

VOID
LvglTpUiDropBackdrop (
  VOID
  );

BOOLEAN
LvglTpUiLive (
  VOID
  );

#endif // LVGL_TEXT_POPUP_H_
