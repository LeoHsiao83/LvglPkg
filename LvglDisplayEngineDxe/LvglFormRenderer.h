/** @file
  LVGL Form Renderer -- builds LVGL widgets from FORM_DISPLAY_ENGINE_FORM.

  Copyright (c) 2024-2026, Hamit Karaca. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef LVGL_FORM_RENDERER_H_
#define LVGL_FORM_RENDERER_H_

#include <Uefi.h>
#include <Library/LvglLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/HiiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/DisplayProtocol.h>
#include <Protocol/FormBrowserEx.h>
#include <Protocol/HiiPopup.h>
#include <Uefi/UefiInternalFormRepresentation.h>

//
// Per-widget user data linking an LVGL object back to its statement.
//
typedef struct {
  FORM_DISPLAY_ENGINE_STATEMENT    *Statement;
  lv_obj_t                        *Widget;
  EFI_HII_HANDLE                   HiiHandle;
} LVGL_STATEMENT_CONTEXT;

//
// Session state for a single FormDisplay() call.
//
typedef struct {
  FORM_DISPLAY_ENGINE_FORM    *FormData;
  USER_INPUT                  *UserInput;
  //
  // Set to TRUE by an event handler when the user has made a selection
  // or pressed ESC. The main loop checks this to break.
  //
  BOOLEAN                     ExitRequested;
  //
  // LVGL screen object for this form. Deleted on exit.
  //
  lv_obj_t                    *Screen;
  //
  // Focused group for keyboard navigation.
  //
  lv_group_t                  *Group;
  //
  // Bottom-docked on-screen keyboard, hidden until a textarea is focused
  // (String / Password / Numeric). Lets mouse-only users type into HII
  // text fields. Lives on Screen so it overlays the form chrome.
  //
  lv_obj_t                    *OnScreenKb;
} LVGL_FORM_SESSION;

/**
  Build LVGL widgets for a form and run the event loop until
  the user performs an action.

  @param[in]  FormData       Parsed form from SetupBrowserDxe.
  @param[out] UserInputData  Filled with the user's selection/action.

  @retval EFI_SUCCESS        User input captured.
**/
EFI_STATUS
EFIAPI
LvglRenderForm (
  IN  FORM_DISPLAY_ENGINE_FORM  *FormData,
  OUT USER_INPUT                *UserInputData
  );

/**
  Show a save/discard/cancel confirmation popup and block until the user
  chooses. Called from LvglConfirmDataChange() in LvglDisplayEngineDxe.c.

  @return BROWSER_ACTION_SUBMIT   User chose Save.
  @return BROWSER_ACTION_DISCARD  User chose Discard.
  @return BROWSER_ACTION_NONE     User chose Cancel (stay in form).
**/
UINTN
EFIAPI
LvglRunConfirmPopup (
  VOID
  );

/**
  Show a modal popup (backing EFI_HII_POPUP_PROTOCOL.CreatePopup) and block
  until the user makes a selection. Used by driver callbacks such as
  SecureBootConfigDxe's "Reset Secure Boot Keys" Yes/No confirmation.

  @param[in]  PopupStyle     Info / Warning / Error styling.
  @param[in]  PopupType      Ok, OkCancel, YesNo or YesNoCancel button set.
  @param[in]  HiiHandle      HII handle owning the message string.
  @param[in]  Message        String token for the message body.
  @param[out] UserSelection  The button the user chose (optional).

  @retval EFI_SUCCESS            Popup shown and a selection captured.
  @retval EFI_INVALID_PARAMETER  HiiHandle/Message did not resolve to a string.
  @retval EFI_DEVICE_ERROR       LVGL is not ready to display.
**/
EFI_STATUS
EFIAPI
LvglHiiCreatePopup (
  IN  EFI_HII_POPUP_STYLE      PopupStyle,
  IN  EFI_HII_POPUP_TYPE       PopupType,
  IN  EFI_HII_HANDLE           HiiHandle,
  IN  EFI_STRING_ID            Message,
  OUT EFI_HII_POPUP_SELECTION  *UserSelection OPTIONAL
  );

/**
  Save the whole screen as a BMP file in the root of the first writable USB
  volume (LvglScreenshot.c).

  @param[out] FileName      Name of the saved file.
  @param[in]  FileNameSize  Size of FileName in bytes.
  @param[out] FileSize      Size of the saved file in bytes.

  @retval EFI_SUCCESS    The file was written.
  @retval EFI_NOT_FOUND  No USB volume; nothing was captured.
  @retval Others         Capture or write failure.
**/
EFI_STATUS
LvglScreenshotSave (
  OUT CHAR16  *FileName,
  IN  UINTN   FileNameSize,
  OUT UINT32  *FileSize
  );

/**
  Tear down LVGL objects created by the renderer.
**/
VOID
EFIAPI
LvglRendererCleanup (
  VOID
  );

#endif // LVGL_FORM_RENDERER_H_
