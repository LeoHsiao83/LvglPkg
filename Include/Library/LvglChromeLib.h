/** @file
  Platform-swappable Setup chrome library class.

  Copyright (c) 2026, LeoHsiao83. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef LVGL_CHROME_LIB_H_
#define LVGL_CHROME_LIB_H_

#include <Uefi.h>
#include <Library/LvglLib.h>
#include <Protocol/DisplayProtocol.h>

#define LVGL_TAB_MAX        12
#define LVGL_TAB_TITLE_MAX  48

typedef enum {
  LvglTabKindRef,        ///< Front-page REF statement.
  LvglTabKindSaveExit    ///< Synthetic Save & Exit page drawn by the engine.
} LVGL_TAB_KIND;

typedef struct {
  LVGL_TAB_KIND  Kind;
  UINT16         QuestionId;                    ///< Front-page REF question (Kind == Ref).
  EFI_GUID       FormSetGuid;                   ///< Zero when the REF carries none.
  CHAR8          Title[LVGL_TAB_TITLE_MAX];     ///< UTF-8; the chrome may replace it.
} LVGL_TAB;

typedef struct {
  UINTN     Count;
  UINTN     Active;      ///< Index into Tab[].
  BOOLEAN   AtRoot;      ///< Current form is the root form of the active tab.
  LVGL_TAB  Tab[LVGL_TAB_MAX];
} LVGL_TAB_BAR;

/**
  Adjust the tab list collected from the front page: reorder, rename, drop
  entries or append a LvglTabKindSaveExit entry. Called on every front-page
  FormDisplay with a freshly collected list, so it must be idempotent.
**/
VOID
EFIAPI
LvglChromeAdjustTabs (
  IN OUT LVGL_TAB_BAR  *Tabs
  );

/**
  Build chrome under Screen and return the content panel where the form
  renderer places statement widgets.

  @param[in] Screen    LVGL root screen (already created).
  @param[in] FormData  Current form (subtitle / title source).
  @param[in] Tabs      Optional tab bar (NULL = not in tab mode).

  @return Content panel owned by Screen.
**/
lv_obj_t *
EFIAPI
LvglChromeBuild (
  IN lv_obj_t                  *Screen,
  IN FORM_DISPLAY_ENGINE_FORM  *FormData,
  IN CONST LVGL_TAB_BAR        *Tabs OPTIONAL
  );

/**
  Tear down chrome-owned LVGL timers before the next LvglChromeBuild.
  Timers are not children of the screen object, so deleting the screen
  alone does not stop them.
**/
VOID
EFIAPI
LvglChromeTeardown (
  VOID
  );

/**
  Update the help-pane text on focus or mouse-hover change.
  Utf8 may be NULL or an empty string to clear the pane.
**/
VOID
EFIAPI
LvglChromeSetHelpText (
  IN CONST CHAR8  *Utf8
  );

/**
  Identifies which chrome-styled object a StyleWidget call targets.
  Numeric questions use a digit textarea (not a spinbox), so the part is
  named Numeric rather than Spinbox.
**/
typedef enum {
  LvglChromePartPrompt,          ///< Question prompt label in a row.
  LvglChromePartValueText,       ///< Read-only value (TEXT second string).
  LvglChromePartSubtitle,        ///< SUBTITLE label.
  LvglChromePartBanner,          ///< Front-page banner line.
  LvglChromePartRefButton,       ///< REF / ACTION row button.
  LvglChromePartRefLabel,        ///< Text inside a REF / ACTION button.
  LvglChromePartCheckbox,
  LvglChromePartDropdown,        ///< ONE_OF closed control.
  LvglChromePartDropdownList,    ///< ONE_OF opened list.
  LvglChromePartNumeric,         ///< NUMERIC digit textarea.
  LvglChromePartTextarea,        ///< STRING / PASSWORD input.
  LvglChromePartDateTimeField,
  LvglChromePartOrderedList,
  LvglChromePartDialogPanel,
  LvglChromePartDialogTitle,
  LvglChromePartDialogMessage,
  LvglChromePartDialogButton,
  LvglChromePartMax
} LVGL_CHROME_PART;

/**
  Optional chrome restyle of a statement row after the renderer default styles.
**/
VOID
EFIAPI
LvglChromeStyleRow (
  IN lv_obj_t  *Row
  );

/**
  Optional chrome restyle of a control or label after the renderer default styles.
**/
VOID
EFIAPI
LvglChromeStyleWidget (
  IN lv_obj_t          *Obj,
  IN LVGL_CHROME_PART  Part
  );

#endif // LVGL_CHROME_LIB_H_