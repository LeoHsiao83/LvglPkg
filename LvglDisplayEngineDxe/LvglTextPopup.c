/** @file
  Text popup interception -- console hooks, text grid emulation and session
  state.

  The EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL of the GraphicsConsole child on LVGL's
  display is patched in place. gST->ConOut (the splitter) is left alone, so
  serial terminals keep receiving every character. While a session is active
  the child's drawing calls are swallowed and emulated on a cell grid; a
  closed box on the grid becomes an LVGL window.

  Copyright (c) 2026, LeoHsiao83. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/LvglLib.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/GraphicsOutput.h>
#include <Guid/MdeModuleHii.h>
#include "LvglTextPopup.h"

typedef enum {
  TpOff,
  TpInForm,
  TpBetween
} TP_STATE;

BOOLEAN  gLvglTpBusy;

STATIC TP_STATE                         mState;
STATIC EFI_HANDLE                       mHookHandle;
STATIC EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *mHookSto;
STATIC EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  mOrig;
STATIC UINTN                            mDepth;
STATIC BOOLEAN                          mIgnoreClears;
STATIC BOOLEAN                          mRepaint;
STATIC TP_GRID                          mGrid;
STATIC BOOLEAN                          mCornerPending;
STATIC UINTN                            mCornerRow;
STATIC UINTN                            mCornerCol;
STATIC BOOLEAN                          mWinDirty;
STATIC TP_BOX                           mWinBox;
STATIC TP_SCENE                         mScene;
STATIC EFI_EVENT                        mReadyToBootEvent;
STATIC EFI_EVENT                        mExitBootServicesEvent;

/**
  The hooked instance is freed when the console is reconnected; check it is
  still installed before calling into it.
**/
STATIC
BOOLEAN
HookValid (
  VOID
  )
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Sto;

  if (mHookSto == NULL) {
    return FALSE;
  }

  if (EFI_ERROR (gBS->HandleProtocol (mHookHandle, &gEfiSimpleTextOutProtocolGuid, (VOID **)&Sto))) {
    return FALSE;
  }

  return (BOOLEAN)(Sto == mHookSto);
}

STATIC
VOID
GridClear (
  VOID
  )
{
  UINTN  Index;
  UINT8  Attr;

  mCornerPending = FALSE;
  mWinDirty      = FALSE;
  if ((mGrid.Cells == NULL) || (mHookSto == NULL)) {
    return;
  }

  Attr = (UINT8)(mHookSto->Mode->Attribute & 0x7F);
  for (Index = 0; Index < mGrid.Cols * mGrid.Rows; Index++) {
    mGrid.Cells[Index].Ch    = L' ';
    mGrid.Cells[Index].Attr  = Attr;
    mGrid.Cells[Index].Flags = 0;
  }
}

STATIC
VOID
GridPut (
  IN CHAR16  Ch,
  IN UINT8   Flags
  )
{
  UINTN    Row;
  UINTN    Col;
  UINT8    Attr;
  TP_CELL  *Cell;

  Row = (UINTN)mHookSto->Mode->CursorRow;
  Col = (UINTN)mHookSto->Mode->CursorColumn;
  if ((Row >= mGrid.Rows) || (Col >= mGrid.Cols)) {
    return;
  }

  if ((Ch == 0x2518) || (Ch == 0x255D)) {
    mCornerPending = TRUE;
    mCornerRow     = Row;
    mCornerCol     = Col;
  }

  Attr = (UINT8)(mHookSto->Mode->Attribute & 0x7F);
  Cell = &mGrid.Cells[Row * mGrid.Cols + Col];
  if ((Cell->Ch == Ch) && (Cell->Attr == Attr) && (Cell->Flags == Flags)) {
    return;
  }

  Cell->Ch    = Ch;
  Cell->Attr  = Attr;
  Cell->Flags = Flags;
  if (LvglTpUiLive () &&
      (Row >= mWinBox.Top) && (Row <= mWinBox.Bottom) &&
      (Col >= mWinBox.Left) && (Col <= mWinBox.Right))
  {
    mWinDirty = TRUE;
  }
}

/**
  Line feed: move down one row, scrolling the grid at the bottom.
**/
STATIC
VOID
GridLineFeed (
  VOID
  )
{
  UINTN  Index;
  UINT8  Attr;

  if ((UINTN)mHookSto->Mode->CursorRow + 1 < mGrid.Rows) {
    mHookSto->Mode->CursorRow++;
    return;
  }

  CopyMem (mGrid.Cells, mGrid.Cells + mGrid.Cols, (mGrid.Rows - 1) * mGrid.Cols * sizeof (TP_CELL));
  Attr = (UINT8)(mHookSto->Mode->Attribute & 0x7F);
  for (Index = (mGrid.Rows - 1) * mGrid.Cols; Index < mGrid.Rows * mGrid.Cols; Index++) {
    mGrid.Cells[Index].Ch    = L' ';
    mGrid.Cells[Index].Attr  = Attr;
    mGrid.Cells[Index].Flags = 0;
  }

  mCornerPending = FALSE;
  mWinDirty      = LvglTpUiLive ();
}

/**
  Mirror GraphicsConsoleConOutOutputString on the grid and the child's mode.
**/
STATIC
VOID
GridOutputString (
  IN CONST CHAR16  *WString
  )
{
  EFI_SIMPLE_TEXT_OUTPUT_MODE  *Mode;
  INT32                        OrigAttribute;
  BOOLEAN                      Wide;

  Mode          = mHookSto->Mode;
  OrigAttribute = Mode->Attribute;

  for ( ; *WString != CHAR_NULL; WString++) {
    switch (*WString) {
      case CHAR_BACKSPACE:
        if ((Mode->CursorColumn == 0) && (Mode->CursorRow > 0)) {
          Mode->CursorRow--;
          Mode->CursorColumn = (INT32)(mGrid.Cols - 1);
          GridPut (L' ', 0);
        } else if (Mode->CursorColumn > 0) {
          Mode->CursorColumn--;
          GridPut (L' ', 0);
        }

        break;

      case CHAR_LINEFEED:
        GridLineFeed ();
        break;

      case CHAR_CARRIAGE_RETURN:
        Mode->CursorColumn = 0;
        break;

      case WIDE_CHAR:
        Mode->Attribute |= EFI_WIDE_ATTRIBUTE;
        break;

      case NARROW_CHAR:
        Mode->Attribute &= ~(INT32)EFI_WIDE_ATTRIBUTE;
        break;

      default:
        Wide = (BOOLEAN)((Mode->Attribute & EFI_WIDE_ATTRIBUTE) != 0);
        if (Wide && ((UINTN)Mode->CursorColumn + 1 >= mGrid.Cols)) {
          GridPut (L' ', 0);
          Mode->CursorColumn = 0;
          GridLineFeed ();
        }

        GridPut (*WString, 0);
        Mode->CursorColumn++;
        if (Wide) {
          GridPut (L' ', TP_CELL_WIDE_TAIL);
          Mode->CursorColumn++;
        }

        if ((UINTN)Mode->CursorColumn >= mGrid.Cols) {
          Mode->CursorColumn = 0;
          GridLineFeed ();
        }

        break;
    }
  }

  Mode->Attribute = OrigAttribute;
}

STATIC
VOID
ShowBox (
  IN CONST TP_BOX  *Box
  )
{
  TpBuildScene (&mGrid, Box, &mScene);
  CopyMem (&mWinBox, Box, sizeof (mWinBox));
  mWinDirty = FALSE;
  LvglTpUiShow (&mScene);
}

/**
  Show a box closed by this call, or refresh the live window if this call
  changed cells inside it.
**/
STATIC
VOID
Commit (
  VOID
  )
{
  TP_BOX  Box;

  if (mCornerPending) {
    mCornerPending = FALSE;
    if (TpFindClosedBox (&mGrid, mCornerRow, mCornerCol, &Box)) {
      ShowBox (&Box);
      return;
    }
  }

  if (mWinDirty) {
    mWinDirty = FALSE;
    if (TpFindClosedBox (&mGrid, mWinBox.Bottom, mWinBox.Right, &Box) &&
        (CompareMem (&Box, &mWinBox, sizeof (Box)) == 0))
    {
      ShowBox (&Box);
    } else {
      LvglTpUiDismiss (TRUE);
    }
  }
}

STATIC
BOOLEAN
CanRender (
  VOID
  )
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;
  lv_display_t                  *Display;

  if ((mState == TpOff) || gLvglTpBusy || (mGrid.Cells == NULL)) {
    return FALSE;
  }

  if (EfiGetCurrentTpl () > TPL_NOTIFY) {
    return FALSE;
  }

  Display = lv_display_get_default ();
  if (Display == NULL) {
    return FALSE;
  }

  if (EFI_ERROR (gBS->HandleProtocol (mHookHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop))) {
    return FALSE;
  }

  return (BOOLEAN)((Gop->Mode->Info->HorizontalResolution == (UINT32)lv_display_get_horizontal_resolution (Display)) &&
                   (Gop->Mode->Info->VerticalResolution == (UINT32)lv_display_get_vertical_resolution (Display)));
}

/**
  Start a hooked call.

  @retval TRUE   Emulate the call on the grid; finish with Leave().
  @retval FALSE  Pass the call through, then decrement mDepth.
**/
STATIC
BOOLEAN
Enter (
  VOID
  )
{
  if (mDepth++ > 0) {
    return FALSE;
  }

  if (mState == TpOff) {
    return FALSE;
  }

  if (CanRender ()) {
    return TRUE;
  }

  GridClear ();
  mRepaint = TRUE;
  return FALSE;
}

STATIC
VOID
Leave (
  VOID
  )
{
  mDepth--;
  Commit ();
}

STATIC
EFI_STATUS
EFIAPI
TpOutputString (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN CHAR16                           *WString
  )
{
  EFI_STATUS  Status;

  if (!Enter ()) {
    Status = mOrig.OutputString (This, WString);
    mDepth--;
    return Status;
  }

  GridOutputString (WString);
  Leave ();
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TpClearScreen (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This
  )
{
  EFI_STATUS  Status;

  if (!Enter ()) {
    Status = mOrig.ClearScreen (This);
    mDepth--;
    return Status;
  }

  GridClear ();
  This->Mode->CursorColumn = 0;
  This->Mode->CursorRow    = 0;
  if (!mIgnoreClears) {
    LvglTpUiDismiss (TRUE);
  }

  mDepth--;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
TpSetAttribute (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN UINTN                            Attribute
  )
{
  EFI_STATUS  Status;

  if (!Enter ()) {
    Status = mOrig.SetAttribute (This, Attribute);
    mDepth--;
    return Status;
  }

  Status = EFI_UNSUPPORTED;
  if ((Attribute | 0x7F) == 0x7F) {
    This->Mode->Attribute = (INT32)Attribute;
    Status                = EFI_SUCCESS;
  }

  Leave ();
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TpSetCursorPosition (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN UINTN                            Column,
  IN UINTN                            Row
  )
{
  EFI_STATUS  Status;

  if (!Enter ()) {
    Status = mOrig.SetCursorPosition (This, Column, Row);
    mDepth--;
    return Status;
  }

  Status = EFI_UNSUPPORTED;
  if ((Column < mGrid.Cols) && (Row < mGrid.Rows)) {
    This->Mode->CursorColumn = (INT32)Column;
    This->Mode->CursorRow    = (INT32)Row;
    Status                   = EFI_SUCCESS;
  }

  Leave ();
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
TpEnableCursor (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN BOOLEAN                          Visible
  )
{
  EFI_STATUS  Status;

  if (!Enter ()) {
    Status = mOrig.EnableCursor (This, Visible);
    mDepth--;
    return Status;
  }

  This->Mode->CursorVisible = Visible;
  Leave ();
  return EFI_SUCCESS;
}

//
// A swallowed mode change would leave GraphicsConsole's line buffer sized for
// the old mode, so SetMode and Reset always end the session and pass through.
//
STATIC
EFI_STATUS
EFIAPI
TpSetMode (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN UINTN                            ModeNumber
  )
{
  if (mDepth == 0) {
    LvglTpEnd ();
  }

  return mOrig.SetMode (This, ModeNumber);
}

STATIC
EFI_STATUS
EFIAPI
TpReset (
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN BOOLEAN                          ExtendedVerification
  )
{
  if (mDepth == 0) {
    LvglTpEnd ();
  }

  return mOrig.Reset (This, ExtendedVerification);
}

STATIC
VOID
Unhook (
  VOID
  )
{
  if (HookValid () && (mHookSto->OutputString == TpOutputString)) {
    CopyMem (mHookSto, &mOrig, sizeof (mOrig));
  }

  mHookSto    = NULL;
  mHookHandle = NULL;
}

/**
  Hook the text output child on LVGL's display. Skips the splitter's virtual
  handles, which also carry a GOP when there is a single physical display.
**/
STATIC
BOOLEAN
EnsureHook (
  VOID
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Index;
  EFI_HANDLE                       Preferred;
  EFI_HANDLE                       Handle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Sto;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *Found;
  VOID                             *Gop;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleTextOutProtocolGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Preferred = lv_uefi_display_get_any ();
  Found     = NULL;
  Handle    = NULL;
  for (Index = 0; Index < Count; Index++) {
    if ((Handles[Index] == gST->ConsoleOutHandle) || (Handles[Index] == gST->StandardErrorHandle)) {
      continue;
    }

    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiGraphicsOutputProtocolGuid, &Gop)) ||
        EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleTextOutProtocolGuid, (VOID **)&Sto)))
    {
      continue;
    }

    if ((Found == NULL) || (Handles[Index] == Preferred)) {
      Found  = Sto;
      Handle = Handles[Index];
    }
  }

  FreePool (Handles);
  if (Found == NULL) {
    return FALSE;
  }

  if ((Found == mHookSto) && (Found->OutputString == TpOutputString)) {
    return TRUE;
  }

  Unhook ();
  CopyMem (&mOrig, Found, sizeof (mOrig));
  mHookSto    = Found;
  mHookHandle = Handle;

  Found->Reset             = TpReset;
  Found->OutputString      = TpOutputString;
  Found->SetMode           = TpSetMode;
  Found->SetAttribute      = TpSetAttribute;
  Found->ClearScreen       = TpClearScreen;
  Found->SetCursorPosition = TpSetCursorPosition;
  Found->EnableCursor      = TpEnableCursor;

  DEBUG ((DEBUG_INFO, "LvglTextPopup: hooked text output %p on handle %p\n", Found, Handle));
  return TRUE;
}

STATIC
BOOLEAN
GridSetup (
  VOID
  )
{
  UINTN  Cols;
  UINTN  Rows;

  if (EFI_ERROR (mOrig.QueryMode (mHookSto, mHookSto->Mode->Mode, &Cols, &Rows)) ||
      (Cols == 0) || (Rows == 0))
  {
    return FALSE;
  }

  if ((Cols != mGrid.Cols) || (Rows != mGrid.Rows)) {
    if (mGrid.Cells != NULL) {
      FreePool (mGrid.Cells);
    }

    mGrid.Cells = AllocatePool (Cols * Rows * sizeof (TP_CELL));
    if (mGrid.Cells == NULL) {
      mGrid.Cols = 0;
      mGrid.Rows = 0;
      return FALSE;
    }

    mGrid.Cols = Cols;
    mGrid.Rows = Rows;
    DEBUG ((DEBUG_INFO, "LvglTextPopup: text grid %ux%u\n", Cols, Rows));
  }

  GridClear ();
  return TRUE;
}

VOID
LvglTpFormEnter (
  VOID
  )
{
  LvglTpUiDismiss (FALSE);
  if (!EnsureHook () || !GridSetup ()) {
    LvglTpEnd ();
    return;
  }

  if (mState == TpOff) {
    DEBUG ((DEBUG_INFO, "LvglTextPopup: session begins\n"));
  }

  mState = TpInForm;
}

VOID
LvglTpFormLeave (
  VOID
  )
{
  if (mState == TpInForm) {
    mState = TpBetween;
  }
}

/**
  End the session and resync GraphicsConsole: its XOR cursor no longer
  matches the screen, so clear with the cursor hidden, then restore it.
**/
VOID
LvglTpEnd (
  VOID
  )
{
  BOOLEAN  Visible;

  if (mState == TpOff) {
    return;
  }

  mState = TpOff;
  GridClear ();
  if (HookValid ()) {
    Visible                         = mHookSto->Mode->CursorVisible;
    mHookSto->Mode->CursorVisible   = FALSE;
    mOrig.ClearScreen (mHookSto);
    mOrig.EnableCursor (mHookSto, Visible);
  }

  DEBUG ((DEBUG_INFO, "LvglTextPopup: session ends\n"));
}

VOID
LvglTpIgnoreClears (
  IN BOOLEAN  Ignore
  )
{
  mIgnoreClears = Ignore;
}

/**
  Called by the LVGL loop before each frame. The loop only runs again once
  the popup's owner has returned, so any window is stale by now.
**/
VOID
LvglTpFrame (
  VOID
  )
{
  if (LvglTpUiLive ()) {
    LvglTpUiDismiss (FALSE);
    GridClear ();
  }

  if (mRepaint) {
    mRepaint = FALSE;
    if (lv_screen_active () != NULL) {
      lv_obj_invalidate (lv_screen_active ());
    }
  }
}

STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  LvglTpEnd ();
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  mState = TpOff;
}

VOID
LvglTpInit (
  VOID
  )
{
  EfiCreateEventReadyToBootEx (TPL_CALLBACK, OnReadyToBoot, NULL, &mReadyToBootEvent);
  gBS->CreateEventEx (
         EVT_NOTIFY_SIGNAL,
         TPL_NOTIFY,
         OnExitBootServices,
         NULL,
         &gEfiEventExitBootServicesGuid,
         &mExitBootServicesEvent
         );
}

VOID
LvglTpUnload (
  VOID
  )
{
  LvglTpEnd ();
  Unhook ();
  gBS->CloseEvent (mReadyToBootEvent);
  gBS->CloseEvent (mExitBootServicesEvent);
  if (mGrid.Cells != NULL) {
    FreePool (mGrid.Cells);
    mGrid.Cells = NULL;
  }
}
