/** @file
  Text popup interception -- box finder and scene builder.

  Works only on the emulated text grid; no LVGL calls.

  Copyright (c) 2026, LeoHsiao83. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseMemoryLib.h>
#include "LvglTextPopup.h"

#define TP_AT(Grid, R, C)  ((Grid)->Cells[(R) * (Grid)->Cols + (C)])

STATIC
BOOLEAN
IsHoriz (
  IN CHAR16  Ch
  )
{
  return (BOOLEAN)((Ch == 0x2500) || (Ch == 0x2550));
}

STATIC
BOOLEAN
IsVert (
  IN CHAR16  Ch
  )
{
  return (BOOLEAN)((Ch == 0x2502) || (Ch == 0x2551));
}

STATIC
BOOLEAN
IsLeftDivider (
  IN CHAR16  Ch
  )
{
  return (BOOLEAN)((Ch == 0x251C) || (Ch == 0x255F) || (Ch == 0x2560));
}

STATIC
BOOLEAN
IsRightDivider (
  IN CHAR16  Ch
  )
{
  return (BOOLEAN)((Ch == 0x2524) || (Ch == 0x2562) || (Ch == 0x2563));
}

BOOLEAN
TpFindClosedBox (
  IN  CONST TP_GRID  *Grid,
  IN  UINTN          Row,
  IN  UINTN          Col,
  OUT TP_BOX         *Box
  )
{
  UINTN   Left;
  UINTN   Top;
  UINTN   Index;
  CHAR16  Ch;

  if ((Row >= Grid->Rows) || (Col >= Grid->Cols)) {
    return FALSE;
  }

  Ch = TP_AT (Grid, Row, Col).Ch;
  if ((Ch != 0x2518) && (Ch != 0x255D)) {
    return FALSE;
  }

  for (Left = Col; ; ) {
    if (Left == 0) {
      return FALSE;
    }

    Ch = TP_AT (Grid, Row, --Left).Ch;
    if ((Ch == 0x2514) || (Ch == 0x255A)) {
      break;
    }

    if (!IsHoriz (Ch)) {
      return FALSE;
    }
  }

  for (Top = Row; ; ) {
    if (Top == 0) {
      return FALSE;
    }

    Ch = TP_AT (Grid, --Top, Col).Ch;
    if ((Ch == 0x2510) || (Ch == 0x2557)) {
      break;
    }

    if (!IsVert (Ch) && !IsRightDivider (Ch)) {
      return FALSE;
    }
  }

  if ((Col - Left < 2) || (Row - Top < 2)) {
    return FALSE;
  }

  Ch = TP_AT (Grid, Top, Left).Ch;
  if ((Ch != 0x250C) && (Ch != 0x2554)) {
    return FALSE;
  }

  for (Index = Left + 1; Index < Col; Index++) {
    if (!IsHoriz (TP_AT (Grid, Top, Index).Ch)) {
      return FALSE;
    }
  }

  for (Index = Top + 1; Index < Row; Index++) {
    Ch = TP_AT (Grid, Index, Left).Ch;
    if (!IsVert (Ch) && !IsLeftDivider (Ch)) {
      return FALSE;
    }
  }

  Box->Top    = Top;
  Box->Left   = Left;
  Box->Bottom = Row;
  Box->Right  = Col;
  return TRUE;
}

/**
  Convert one inner box row to trimmed UTF-8, dropping box-drawing and
  block characters.
**/
STATIC
VOID
RowToUtf8 (
  IN  CONST TP_GRID  *Grid,
  IN  CONST TP_BOX   *Box,
  IN  UINTN          Row,
  OUT CHAR8          *Out
  )
{
  UINTN   Col;
  UINTN   Len;
  UINTN   Start;
  CHAR16  Ch;

  Len = 0;
  for (Col = Box->Left + 1; (Col < Box->Right) && (Len + 4 < TP_TEXT_MAX); Col++) {
    if ((TP_AT (Grid, Row, Col).Flags & TP_CELL_WIDE_TAIL) != 0) {
      continue;
    }

    Ch = TP_AT (Grid, Row, Col).Ch;
    //
    // Scroll triangles and arrows become LV_SYMBOL_UP / LV_SYMBOL_DOWN;
    // Montserrat has no glyphs for them.
    //
    if ((Ch == 0x25B2) || (Ch == 0x2191) || (Ch == 0x25BC) || (Ch == 0x2193)) {
      CopyMem (Out + Len, ((Ch == 0x25B2) || (Ch == 0x2191)) ? "\xEF\x81\xB7" : "\xEF\x81\xB8", 3);
      Len += 3;
      continue;
    }

    if ((Ch < 0x20) || ((Ch >= 0x2500) && (Ch <= 0x259F))) {
      Ch = L' ';
    }

    if (Ch < 0x80) {
      Out[Len++] = (CHAR8)Ch;
    } else if (Ch < 0x800) {
      Out[Len++] = (CHAR8)(0xC0 | (Ch >> 6));
      Out[Len++] = (CHAR8)(0x80 | (Ch & 0x3F));
    } else {
      Out[Len++] = (CHAR8)(0xE0 | (Ch >> 12));
      Out[Len++] = (CHAR8)(0x80 | ((Ch >> 6) & 0x3F));
      Out[Len++] = (CHAR8)(0x80 | (Ch & 0x3F));
    }
  }

  while ((Len > 0) && (Out[Len - 1] == ' ')) {
    Len--;
  }

  for (Start = 0; (Start < Len) && (Out[Start] == ' '); Start++) {
  }

  CopyMem (Out, Out + Start, Len - Start);
  Out[Len - Start] = '\0';
}

VOID
TpBuildScene (
  IN  CONST TP_GRID  *Grid,
  IN  CONST TP_BOX   *Box,
  OUT TP_SCENE       *Scene
  )
{
  UINTN  Row;
  UINTN  Dividers;
  UINTN  Section;
  UINTN  Index;
  UINT8  BoxBg;

  ZeroMem (Scene, sizeof (*Scene));
  CopyMem (&Scene->Box, Box, sizeof (*Box));
  Scene->GridCols = Grid->Cols;
  BoxBg = TP_AT (Grid, Box->Bottom, Box->Right).Attr >> 4;

  Dividers = 0;
  for (Row = Box->Top + 1; Row < Box->Bottom; Row++) {
    if (IsLeftDivider (TP_AT (Grid, Row, Box->Left).Ch) && IsRightDivider (TP_AT (Grid, Row, Box->Right).Ch)) {
      Dividers++;
    }
  }

  Section = 0;
  for (Row = Box->Top + 1; (Row < Box->Bottom) && (Scene->RowCount < TP_ROW_MAX); Row++) {
    if (IsLeftDivider (TP_AT (Grid, Row, Box->Left).Ch) && IsRightDivider (TP_AT (Grid, Row, Box->Right).Ch)) {
      Section++;
      continue;
    }

    Index = Scene->RowCount++;
    RowToUtf8 (Grid, Box, Row, Scene->Row[Index].Text);
    Scene->Row[Index].Highlight = (BOOLEAN)((TP_AT (Grid, Row, Box->Left + 1).Attr >> 4) != BoxBg);
    if ((Dividers > 0) && (Section == 0)) {
      Scene->Row[Index].Kind = TpRowTitle;
    } else if ((Dividers > 1) && (Section == Dividers)) {
      Scene->Row[Index].Kind = TpRowFooter;
    } else {
      Scene->Row[Index].Kind = TpRowBody;
    }
  }

  if (Dividers > 0) {
    return;
  }

  //
  // Plain dialogs center every line; the first line becomes the title when
  // more text follows it.
  //
  Scene->Centered = TRUE;
  for (Index = 0; Index < Scene->RowCount; Index++) {
    if (Scene->Row[Index].Text[0] != '\0') {
      break;
    }
  }

  for (Row = Index + 1; Row < Scene->RowCount; Row++) {
    if (Scene->Row[Row].Text[0] != '\0') {
      Scene->Row[Index].Kind = TpRowTitle;
      break;
    }
  }
}
