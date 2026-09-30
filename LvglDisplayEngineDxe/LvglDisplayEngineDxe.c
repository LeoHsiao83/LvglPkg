/** @file
  LVGL-based Display Engine DXE driver.

  Hot-patches the EDKII_FORM_DISPLAY_ENGINE_PROTOCOL and EFI_HII_POPUP_PROTOCOL
  instances installed by the text DisplayEngineDxe. Each call is routed to the
  LVGL renderer when the console has a graphics output, otherwise to the saved
  text implementation.

  Copyright (c) 2024-2026, Hamit Karaca. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/DisplayProtocol.h>
#include <Protocol/GraphicsOutput.h>
#include "LvglFormRenderer.h"

//
// Protocol instances owned by the text DisplayEngineDxe. SetupBrowserDxe
// caches the form display pointer on first use, so the fields are patched in
// place rather than the protocols being reinstalled.
//
STATIC EDKII_FORM_DISPLAY_ENGINE_PROTOCOL  *mFormDisplay;
STATIC EFI_HII_POPUP_PROTOCOL              *mHiiPopup;

//
// Copies of the text engine's original function pointers.
//
STATIC EDKII_FORM_DISPLAY_ENGINE_PROTOCOL  mTextFormDisplay;
STATIC EFI_HII_POPUP_PROTOCOL              mTextHiiPopup;

//
// TRUE once LVGL has rendered a form in the current browser session.
//
STATIC BOOLEAN  mLvglUsed;

/**
  Check whether the console output has a graphics output.

  @retval TRUE   ConOut carries EFI_GRAPHICS_OUTPUT_PROTOCOL.
  @retval FALSE  Text-only console (e.g. serial / headless).
**/
STATIC
BOOLEAN
IsGraphicsConsole (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop;

  Status = gBS->HandleProtocol (
                  gST->ConsoleOutHandle,
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **)&Gop
                  );
  return (BOOLEAN)!EFI_ERROR (Status);
}

/**
  EFI_HII_POPUP_PROTOCOL.CreatePopup thunk -- delegates to the LVGL renderer's
  modal popup while an LVGL form is on screen, otherwise to the text popup.
  Driver callbacks (e.g. SecureBootConfigDxe's "Reset Secure Boot Keys" Yes/No
  confirmation) locate this protocol.
**/
STATIC
EFI_STATUS
EFIAPI
LvglHiiPopupCreate (
  IN  EFI_HII_POPUP_PROTOCOL   *This,
  IN  EFI_HII_POPUP_STYLE      PopupStyle,
  IN  EFI_HII_POPUP_TYPE       PopupType,
  IN  EFI_HII_HANDLE           HiiHandle,
  IN  EFI_STRING_ID            Message,
  OUT EFI_HII_POPUP_SELECTION  *UserSelection OPTIONAL
  )
{
  if (mLvglUsed && IsGraphicsConsole ()) {
    return LvglHiiCreatePopup (PopupStyle, PopupType, HiiHandle, Message, UserSelection);
  }

  return mTextHiiPopup.CreatePopup (This, PopupStyle, PopupType, HiiHandle, Message, UserSelection);
}

/**
  Display one form and return user input.

  @param[in]  FormData        Form data to be displayed.
  @param[out] UserInputData   User input result.

  @retval EFI_SUCCESS         Form displayed and user input captured.
**/
STATIC
EFI_STATUS
EFIAPI
LvglFormDisplay (
  IN  FORM_DISPLAY_ENGINE_FORM  *FormData,
  OUT USER_INPUT                *UserInputData
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "LvglDisplayEngine: FormDisplay() called -- FormId=0x%x\n", FormData->FormId));

  //
  // Any other BrowserStatus asks only for an error popup: the text engine
  // shows it and returns without drawing the form.
  //
  if (IsGraphicsConsole () && (FormData->BrowserStatus == BROWSER_SUCCESS)) {
    //
    // LvglRenderForm only fails in UefiLvglInit, before anything is drawn.
    //
    Status = LvglRenderForm (FormData, UserInputData);
    if (!EFI_ERROR (Status)) {
      mLvglUsed = TRUE;
      return Status;
    }
  }

  return mTextFormDisplay.FormDisplay (FormData, UserInputData);
}

/**
  Exit display and clean up.
**/
STATIC
VOID
EFIAPI
LvglExitDisplay (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "LvglDisplayEngine: ExitDisplay() called\n"));

  if (mLvglUsed) {
    LvglRendererCleanup ();
    mLvglUsed = FALSE;
  }

  mTextFormDisplay.ExitDisplay ();
}

/**
  Confirm how to handle changed data (submit / discard / none).

  @return Action -- BROWSER_ACTION_SUBMIT, BROWSER_ACTION_DISCARD, or BROWSER_ACTION_NONE.
**/
STATIC
UINTN
EFIAPI
LvglConfirmDataChange (
  VOID
  )
{
  DEBUG ((DEBUG_INFO, "LvglDisplayEngine: ConfirmDataChange() called\n"));

  if (mLvglUsed && IsGraphicsConsole ()) {
    return LvglRunConfirmPopup ();
  }

  return mTextFormDisplay.ConfirmDataChange ();
}

/**
  Entry point -- patch the text display engine's protocol instances.

  @param[in] ImageHandle   Driver image handle.
  @param[in] SystemTable   Pointer to EFI System Table.

  @retval EFI_SUCCESS      Protocols patched successfully.
  @return Others           A text display engine protocol was not found.
**/
EFI_STATUS
EFIAPI
LvglDisplayEngineInit (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  DEBUG ((DEBUG_INFO, "LvglDisplayEngine: initializing\n"));

  Status = gBS->LocateProtocol (&gEdkiiFormDisplayEngineProtocolGuid, NULL, (VOID **)&mFormDisplay);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateProtocol (&gEfiHiiPopupProtocolGuid, NULL, (VOID **)&mHiiPopup);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  CopyMem (&mTextFormDisplay, mFormDisplay, sizeof (mTextFormDisplay));
  CopyMem (&mTextHiiPopup, mHiiPopup, sizeof (mTextHiiPopup));

  mFormDisplay->FormDisplay       = LvglFormDisplay;
  mFormDisplay->ExitDisplay       = LvglExitDisplay;
  mFormDisplay->ConfirmDataChange = LvglConfirmDataChange;
  mHiiPopup->CreatePopup          = LvglHiiPopupCreate;

  DEBUG ((DEBUG_INFO, "LvglDisplayEngine: text display engine patched\n"));

  return EFI_SUCCESS;
}

/**
  Unload handler -- restore the text display engine's function pointers.

  @param[in] ImageHandle   Driver image handle.

  @retval EFI_SUCCESS      Original function pointers restored.
**/
EFI_STATUS
EFIAPI
LvglDisplayEngineUnload (
  IN EFI_HANDLE  ImageHandle
  )
{
  CopyMem (mFormDisplay, &mTextFormDisplay, sizeof (mTextFormDisplay));
  CopyMem (mHiiPopup, &mTextHiiPopup, sizeof (mTextHiiPopup));

  DEBUG ((DEBUG_INFO, "LvglDisplayEngine: unloaded\n"));

  return EFI_SUCCESS;
}
