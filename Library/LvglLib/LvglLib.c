
#include "LvglLibCommon.h"

#include <Library/LvglLib.h>
#include <Library/LvglUiConfigLib.h>
#include <Library/LvglThemeLib.h>

extern UINT8  mExitBtnYes;

//
// Read the persisted UI scale selection. Returns LVGL_UI_SCALE_DEFAULT (1x) if
// the variable is absent, malformed, or holds an unknown value.
//
// The variable is only consumed at display-creation time, which happens once a
// GOP is available (BDS). Variable services are up well before that, so the
// value is always readable when it matters.
//
STATIC
UINT8
LvglGetUiScale (
  VOID
  )
{
  LVGL_UI_CONFIG_VARSTORE_DATA  Config;

  LvglUiConfigLoad (&Config);
  return Config.UiScale;
}

// TRUE when LVGL time comes from the TimerLib performance counter. FALSE with a
// null TimerLib; UI loops then advance the tick by their wait period.
BOOLEAN  mTickSupport = FALSE;
STATIC BOOLEAN  mUefiLvglInitDone = FALSE;

STATIC UINT64  mTickCounterStart;
STATIC UINT64  mTickCounterEnd;
STATIC UINT64  mTickCounterLast;
STATIC UINT64  mTickCounterTotal;

//
// The counter may be narrow (24-bit ACPI PM timer wraps every ~4.7 s), so
// accumulate deltas instead of converting the raw value. Gaps longer than one
// wrap between calls undercount, which only slows LVGL time.
//
STATIC
uint32_t
tick_get_cb (
  void
  )
{
  UINT64  Now;
  UINT64  Delta;

  Now = GetPerformanceCounter ();
  if (mTickCounterEnd >= mTickCounterStart) {
    Delta = (Now >= mTickCounterLast) ? Now - mTickCounterLast
                                      : (mTickCounterEnd - mTickCounterLast) + (Now - mTickCounterStart) + 1;
  } else {
    Delta = (Now <= mTickCounterLast) ? mTickCounterLast - Now
                                      : (mTickCounterLast - mTickCounterEnd) + (mTickCounterStart - Now) + 1;
  }

  mTickCounterLast   = Now;
  mTickCounterTotal += Delta;

  return (uint32_t)DivU64x32 (GetTimeInNanoSecond (mTickCounterTotal), 1000 * 1000);
}

STATIC
VOID
UefiLvglTickInit (
  VOID
  )
{
  mTickCounterLast = GetPerformanceCounter ();
  if (mTickCounterLast == 0) {
    return;
  }

  GetPerformanceCounterProperties (&mTickCounterStart, &mTickCounterEnd);
  mTickCounterTotal = 0;
  mTickSupport      = TRUE;
  lv_tick_set_cb (tick_get_cb);
}

#if LV_USE_LOG
static void efi_lv_log_print(lv_log_level_t level, const char * buf)
{
    static const int priority[LV_LOG_LEVEL_NUM] = {
        DEBUG_VERBOSE|DEBUG_INFO|DEBUG_WARN|DEBUG_ERROR, DEBUG_INFO, DEBUG_WARN, DEBUG_ERROR, DEBUG_INFO
    };

    DebugPrint (priority[level], "[LVGL] %a\n", buf);
}
#endif


EFI_STATUS
EFIAPI
UefiLvglInit (
  VOID
  )
{
  EFI_HANDLE                         GopHandle;
  lv_display_t                       *Display;
  UINT8                              UiScale;

  if (mUefiLvglInitDone) {
    return EFI_SUCCESS;
  }

  // lv_uefi_init must be called before lv_init() so the LVGL UEFI backend
  // (lv_uefi_platform_init, invoked from lv_init) finds valid EFI globals.
  lv_uefi_init (gImageHandle, gST);

  lv_init();

  UefiLvglTickInit ();

#if LV_USE_LOG
  lv_log_register_print_cb (efi_lv_log_print);
#endif

  // Use LVGL's built-in UEFI display driver. lv_uefi_display_get_any()
  // returns the first handle with EFI_GRAPHICS_OUTPUT_PROTOCOL installed.
  GopHandle = lv_uefi_display_get_any ();
  if (GopHandle == NULL) {
    lv_deinit ();
    return EFI_UNSUPPORTED;
  }

  //
  // Always render at the physical framebuffer resolution. UiScale selects
  // larger fonts and layout metrics via LvglThemeLib (see LvglTheme.h).
  //
  UiScale = LvglGetUiScale ();
  LvglThemeInitFromUiScale (UiScale);

  Display = lv_uefi_display_create (GopHandle);

  if (Display == NULL) {
    lv_deinit ();
    return EFI_UNSUPPORTED;
  }

  lv_port_indev_init(Display);

  mUefiLvglInitDone = TRUE;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiLvglDeinit (
  VOID
  )
{

  if (!mUefiLvglInitDone) {
    return EFI_SUCCESS;
  }

  LvglUefiEscExitUnregister ();

  lv_deinit();

  lv_port_indev_close();

  gST->ConOut->ClearScreen (gST->ConOut);
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, 0);
  gST->ConOut->EnableCursor (gST->ConOut, TRUE);

  mUefiLvglInitDone = FALSE;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiLvglAppRegister (
  IN EFI_LVGL_APP_FUNCTION AppRegister
  )
{
  if (!mUefiLvglInitDone) {
    if (UefiLvglInit() != EFI_SUCCESS) {
      return EFI_UNSUPPORTED;
    }
  }

  if (AppRegister != NULL) {
    gST->ConOut->ClearScreen (gST->ConOut);
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);

    // call user GUI APP
    AppRegister();

    LvglUefiEscExitRegister ();

    while (1) {
      if (mExitBtnYes == EXIT_BTN_YES) {
        break;
      }

      lv_timer_handler();

      gBS->Stall (10 * 1000);
      if (!mTickSupport) {
        lv_tick_inc(10);
      }
    }
  } else {
    UefiLvglDeinit();
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
LvglLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{

  UefiLvglInit ();

  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
LvglLibDestructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{

  UefiLvglDeinit();

  return EFI_SUCCESS;
}
