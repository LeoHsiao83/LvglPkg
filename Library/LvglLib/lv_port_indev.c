/**
 * @file lv_port_indev.c
 *
 * UEFI input device port: custom pointer indev for mouse + wheel,
 * custom keypad indev for keyboard.
 *
 * --- Pointer ---
 * A single custom read callback (mouse_read) owns the one call to
 * EFI_ABSOLUTE_POINTER_PROTOCOL.GetState() per frame.  This is required
 * because GetState() is single-consumer: the physical USB driver clears its
 * StateChanged flag on each read, and ConSplitter forwards that read to the
 * underlying device.  A separate wheel-poller would therefore steal ~half the
 * pointer's state-change events, degrading cursor tracking.
 *
 * mouse_read handles X/Y rescaling, left-button state, AND Z accumulation for
 * the wheel in a single GetState call.  The built-in LVGL display backend
 * (lv_uefi_display_create, LV_USE_UEFI=1) is unchanged; only the pointer indev
 * reverts to custom.
 *
 * Relative pointers (UsbMouseDxe / Ps2MouseDxe via ConSplitter's
 * EFI_SIMPLE_POINTER_PROTOCOL) follow the same one-GetState-per-frame rule.
 * Counts are converted to pixels through Mode->Resolution* (counts/mm) and
 * LVGL_REL_PIXELS_PER_MM, carrying the remainder so slow motion is not lost.
 * Absolute movement sets the cursor and relative movement offsets it, so
 * whichever device moved last wins.  Relative Z emits one scroll step per
 * wheel detent, independent of the absolute-Z ratchet below.
 *
 * Wheel ratchet: accumulate raw Z counts, emit one scroll step (+/-40 px) per
 * LVGL_WHEEL_COUNTS_PER_DETENT=8 raw counts.  Scroll is applied directly to the
 * nearest scrollable ancestor under the cursor via lv_obj_scroll_by_bounded(),
 * bypassing LVGL's normal click-first-to-scroll-later requirement.
 *
 * Divide-by-zero guard: ConSplitter publishes its virtual AbsolutePointer early
 * with AbsoluteMaxX/Y == 0 until a physical USB mouse binds.  mouse_read checks
 * the live range each frame and simply reports the last cursor position /
 * RELEASED state when the range is zero, so the indev "just starts working"
 * once USB binds -- no retry logic is needed.
 *
 * PR#17 lazy-binding (RegisterProtocolNotify) is kept as cheap insurance for
 * the case where a protocol itself is absent at init time; the callbacks
 * re-grab mAbsPointer / mRelPointer via HandleProtocol.
 *
 * --- Keyboard ---
 * Custom keypad_read returns PRESSED while the EFI console buffer has a key and
 * RELEASED when it is empty.  LVGL's long_press_repeat throttle governs the
 * repeat rate while the user holds a key.  The built-in
 * lv_uefi_simple_text_input_indev was tried but causes runaway navigation
 * because it queues PRESSED+RELEASED in the same tick, bypassing LVGL's rate
 * limiting.
 */


/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include <Library/LvglThemeLib.h>
#include <Protocol/SimplePointer.h>

// Required for lv_uefi_keypad_drain() to poke private keypad state
#include "lvgl/src/indev/lv_indev_private.h"

/*********************
 *      DEFINES
 *********************/

extern const lv_img_dsc_t mouse_cursor_icon;

//
// Wheel ratchet: accumulate this many raw Z counts before emitting one scroll
// step.  Lower = more sensitive.  QEMU usb-mouse can send several reports per
// perceived host wheel motion, so ratcheting prevents over-scrolling.
//
#define LVGL_WHEEL_COUNTS_PER_DETENT  8

//
// Pixels to scroll per detent.  lv_obj_scroll_by_bounded is called directly so
// this value is applied immediately without LVGL's normal indev click precondition.
//
#define LVGL_WHEEL_SCROLL_PIXELS      40

//
// Relative pointer gain in pixels per millimetre of device travel.
// UsbMouseDxe reports 8 counts/mm, so 8 moves one pixel per raw count no
// matter what resolution ConSplitter rescales to.
//
#define LVGL_REL_PIXELS_PER_MM        8

//
// Relative wheel detents per "millimetre" of Z: UsbMouseDxe reports
// ResolutionZ = 8 with one count per detent.
//
#define LVGL_REL_WHEEL_DETENTS_PER_MM 8

/**********************
 *  STATIC VARIABLES
 **********************/

static lv_indev_t *indev_mouse  = NULL;
static lv_indev_t *indev_keypad = NULL;

//
// Pointer state -- owned exclusively by mouse_read.
//
STATIC EFI_ABSOLUTE_POINTER_PROTOCOL *mAbsPointer   = NULL;
STATIC EFI_SIMPLE_POINTER_PROTOCOL   *mRelPointer   = NULL;
STATIC INTN                           mLastCursorX   = 0;
STATIC INTN                           mLastCursorY   = 0;
STATIC UINT64                         mLastAbsX      = 0;
STATIC UINT64                         mLastAbsY      = 0;
STATIC UINT64                         mLastAbsZ      = 0;
STATIC INT32                          mWheelDelta    = 0;
STATIC INT64                          mRelRemX       = 0;
STATIC INT64                          mRelRemY       = 0;
STATIC INT64                          mRelRemZ       = 0;
STATIC BOOLEAN                        mAbsLeftBtn    = FALSE;
STATIC BOOLEAN                        mRelLeftBtn    = FALSE;

//
// The mouse cursor image. Hidden until a pointer (mouse) is actually
// present, so a machine with no mouse doesn't show a dead cursor stuck in the
// middle of the screen.
//
STATIC lv_obj_t                       *mCursorObj    = NULL;
STATIC BOOLEAN                        mCursorVisible = FALSE;

//
// PR#17 lazy-binding state.
//
STATIC EFI_EVENT    mPointerNotifyEvent        = NULL;
STATIC VOID        *mPointerNotifyRegistration = NULL;
STATIC EFI_EVENT    mRelNotifyEvent            = NULL;
STATIC VOID        *mRelNotifyRegistration     = NULL;

/**********************
 *   STATIC FUNCTIONS
 **********************/

//
// Walk from the object under the cursor up to the nearest scrollable ancestor
// that actually has content to scroll.  Returns NULL if none found.
//
static lv_obj_t *
find_scrollable_at_point (
  lv_display_t  *disp,
  lv_point_t    *p
  )
{
  lv_obj_t *screen = lv_display_get_screen_active (disp);
  if (screen == NULL) return NULL;

  lv_obj_t *hit = lv_indev_search_obj (screen, p);
  if (hit == NULL) return NULL;

  for (lv_obj_t *o = hit; o != NULL; o = lv_obj_get_parent (o)) {
    if (lv_obj_has_flag (o, LV_OBJ_FLAG_SCROLLABLE) &&
        lv_obj_get_scroll_top (o) + lv_obj_get_scroll_bottom (o) > 0)
    {
      return o;
    }
  }
  return NULL;
}

//
// Convert relative counts to output units (pixels or wheel steps) through a
// Mode resolution in counts/mm, carrying the remainder in *Rem.
//
static INTN
rel_scale (
  INT32   Counts,
  UINT64  Resolution,
  INTN    UnitsPerMm,
  INT64   *Rem
  )
{
  INT64  Units;

  *Rem  += (INT64)Counts * UnitsPerMm;
  Units  = *Rem / (INT64)Resolution;
  *Rem  -= Units * (INT64)Resolution;
  return (INTN)Units;
}

//
// Custom pointer read callback.
//
// Performs at most one GetState() per pointer protocol per frame, updating
// cursor position, button state, and the wheel accumulators.  Applies wheel
// scroll when the ratchet threshold is crossed or a relative detent arrives.
//
static void
mouse_read (
  lv_indev_t      *indev_drv,
  lv_indev_data_t *data
  )
{
  EFI_STATUS                   Status;
  EFI_ABSOLUTE_POINTER_STATE   AbsState;
  EFI_SIMPLE_POINTER_STATE     RelState;
  BOOLEAN                      AbsUsable;
  BOOLEAN                      RelUsable;
  BOOLEAN                      AbsLeft;
  BOOLEAN                      LeftButton;
  UINT64                       RangeX;
  UINT64                       RangeY;
  INTN                         PrevX;
  INTN                         PrevY;
  int                          wheel_step;
  int                          rel_wheel_step;
  lv_display_t                *disp;
  INT32                        hor_res;
  INT32                        ver_res;

  //
  // Lazy-acquire the protocols -- succeeds after USB/ConSplitter have bound.
  //
  if (mAbsPointer == NULL) {
    gBS->HandleProtocol (
           gST->ConsoleInHandle,
           &gEfiAbsolutePointerProtocolGuid,
           (VOID **)&mAbsPointer);
  }

  if (mRelPointer == NULL) {
    gBS->HandleProtocol (
           gST->ConsoleInHandle,
           &gEfiSimplePointerProtocolGuid,
           (VOID **)&mRelPointer);
  }

  AbsUsable = (mAbsPointer != NULL) &&
              (mAbsPointer->Mode->AbsoluteMaxX != 0) &&
              (mAbsPointer->Mode->AbsoluteMaxY != 0);
  RelUsable = (mRelPointer != NULL) &&
              (mRelPointer->Mode->ResolutionX != 0) &&
              (mRelPointer->Mode->ResolutionY != 0);

  //
  // Report last position + RELEASED if neither protocol is usable yet (absent,
  // or ConSplitter's virtual absolute range still zero before USB binds).
  //
  if (!AbsUsable && !RelUsable) {
    //
    // No pointer device: keep the cursor hidden so it doesn't sit dead in the
    // middle of the screen.
    //
    if (mCursorVisible && (mCursorObj != NULL)) {
      lv_obj_add_flag (mCursorObj, LV_OBJ_FLAG_HIDDEN);
      mCursorVisible = FALSE;
    }

    data->point.x = (lv_coord_t)mLastCursorX;
    data->point.y = (lv_coord_t)mLastCursorY;
    data->state   = LV_INDEV_STATE_RELEASED;
    return;
  }

  disp    = lv_indev_get_display (indev_drv);
  hor_res = lv_display_get_horizontal_resolution (disp);
  ver_res = lv_display_get_vertical_resolution (disp);

  PrevX          = mLastCursorX;
  PrevY          = mLastCursorY;
  rel_wheel_step = 0;

  if (AbsUsable) {
    RangeX = mAbsPointer->Mode->AbsoluteMaxX - mAbsPointer->Mode->AbsoluteMinX;
    RangeY = mAbsPointer->Mode->AbsoluteMaxY - mAbsPointer->Mode->AbsoluteMinY;

    Status = mAbsPointer->GetState (mAbsPointer, &AbsState);
    if (!EFI_ERROR (Status)) {
      AbsLeft = (AbsState.ActiveButtons & BIT0) != 0;

      //
      // Absolute drivers flag a state change on every HID report, so only
      // take the absolute position when it (or the button) actually changed;
      // otherwise an idle absolute device would undo relative movement.
      //
      if ((AbsState.CurrentX != mLastAbsX) ||
          (AbsState.CurrentY != mLastAbsY) ||
          (AbsLeft != mAbsLeftBtn))
      {
        //
        // Rescale absolute X/Y to display pixels, clamp to screen edge.
        //
        mLastCursorX = (INTN)((AbsState.CurrentX * (UINT64)hor_res) / RangeX);
        if (mLastCursorX > hor_res - 1) mLastCursorX = hor_res - 1;
        if (mLastCursorX < 0)           mLastCursorX = 0;

        mLastCursorY = (INTN)((AbsState.CurrentY * (UINT64)ver_res) / RangeY);
        if (mLastCursorY > ver_res - 1) mLastCursorY = ver_res - 1;
        if (mLastCursorY < 0)           mLastCursorY = 0;
      }

      mLastAbsX   = AbsState.CurrentX;
      mLastAbsY   = AbsState.CurrentY;
      mAbsLeftBtn = AbsLeft;

      //
      // Accumulate wheel delta from Z axis.
      // UsbMouseAbsolutePointerDxe clamps CurrentZ to [0, AbsoluteMaxZ=1024]
      // and integrates the HID wheel byte each interrupt, so the delta between
      // successive reads is the wheel motion since the last frame.
      //
      mWheelDelta += (INT32)((INT64)AbsState.CurrentZ - (INT64)mLastAbsZ);
      mLastAbsZ    = AbsState.CurrentZ;
    }
    // On EFI_NOT_READY (no state change since last call) keep last values.
  }

  if (RelUsable) {
    Status = mRelPointer->GetState (mRelPointer, &RelState);
    if (!EFI_ERROR (Status)) {
      mLastCursorX += rel_scale (
                        RelState.RelativeMovementX,
                        mRelPointer->Mode->ResolutionX,
                        LVGL_REL_PIXELS_PER_MM,
                        &mRelRemX);
      if (mLastCursorX > hor_res - 1) mLastCursorX = hor_res - 1;
      if (mLastCursorX < 0)           mLastCursorX = 0;

      mLastCursorY += rel_scale (
                        RelState.RelativeMovementY,
                        mRelPointer->Mode->ResolutionY,
                        LVGL_REL_PIXELS_PER_MM,
                        &mRelRemY);
      if (mLastCursorY > ver_res - 1) mLastCursorY = ver_res - 1;
      if (mLastCursorY < 0)           mLastCursorY = 0;

      //
      // ConSplitter drops Z from children with ResolutionZ == 0 (Ps2MouseDxe).
      //
      if (mRelPointer->Mode->ResolutionZ != 0) {
        rel_wheel_step = (int)rel_scale (
                                RelState.RelativeMovementZ,
                                mRelPointer->Mode->ResolutionZ,
                                LVGL_REL_WHEEL_DETENTS_PER_MM,
                                &mRelRemZ);
      }

      mRelLeftBtn = RelState.LeftButton;
    }
  }

  LeftButton = mAbsLeftBtn || mRelLeftBtn;

  //
  // Reveal the cursor only on genuine pointer activity (movement or a
  // button). A present protocol alone is not enough: the ConSplitter exposes
  // virtual absolute and simple pointers even when no physical mouse is
  // attached. Gating on real motion/click ensures a mouseless machine never
  // shows a dead cursor stuck in the middle of the screen.
  //
  if (!mCursorVisible && (mCursorObj != NULL) &&
      ((mLastCursorX != PrevX) || (mLastCursorY != PrevY) || LeftButton))
  {
    lv_obj_clear_flag (mCursorObj, LV_OBJ_FLAG_HIDDEN);
    mCursorVisible = TRUE;
  }

  data->point.x = (lv_coord_t)mLastCursorX;
  data->point.y = (lv_coord_t)mLastCursorY;
  data->state   = LeftButton ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->enc_diff = 0;

  //
  // Ratchet: emit one scroll step per LVGL_WHEEL_COUNTS_PER_DETENT raw counts.
  //
  wheel_step = 0;
  if (mWheelDelta >= LVGL_WHEEL_COUNTS_PER_DETENT) {
    wheel_step    =  1;
    mWheelDelta  -= LVGL_WHEEL_COUNTS_PER_DETENT;
  } else if (mWheelDelta <= -LVGL_WHEEL_COUNTS_PER_DETENT) {
    wheel_step    = -1;
    mWheelDelta  += LVGL_WHEEL_COUNTS_PER_DETENT;
  }
  wheel_step += rel_wheel_step;

  if (wheel_step != 0) {
    lv_point_t p = { (lv_coord_t)mLastCursorX, (lv_coord_t)mLastCursorY };
    lv_obj_t  *target = find_scrollable_at_point (disp, &p);
    if (target != NULL) {
      lv_obj_scroll_by_bounded (
        target,
        0,
        wheel_step * LVGL_WHEEL_SCROLL_PIXELS,
        LV_ANIM_OFF
        );
    }
  }
}

//
// Custom keyboard read callback.
//
// Returns PRESSED (with the translated key code) when the EFI console buffer
// holds a keystroke, RELEASED when the buffer is empty.  Reading one key per
// call means LVGL sees the key as held across ticks while EFI auto-repeat keeps
// the buffer non-empty, allowing LVGL's long_press_repeat throttle to govern
// the navigation rate.
//
static void
keypad_read (
  lv_indev_t      *indev_drv,
  lv_indev_data_t *data
  )
{
  EFI_STATUS                         Status;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *TxtInEx;
  EFI_KEY_DATA                       KeyData;
  UINT32                             KeyShift;

  data->key = 0;

  Status = gBS->HandleProtocol (
                  gST->ConsoleInHandle,
                  &gEfiSimpleTextInputExProtocolGuid,
                  (VOID **)&TxtInEx);
  if (EFI_ERROR (Status)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  Status = TxtInEx->ReadKeyStrokeEx (TxtInEx, &KeyData);
  if (!EFI_ERROR (Status)) {
    switch (KeyData.Key.UnicodeChar) {
    case CHAR_CARRIAGE_RETURN:
      data->key = LV_KEY_ENTER;
      break;

    case CHAR_BACKSPACE:
      data->key = LV_KEY_BACKSPACE;
      break;

    case CHAR_TAB:
      KeyShift = KeyData.KeyState.KeyShiftState;
      data->key = ((KeyShift & EFI_SHIFT_STATE_VALID) &&
                   (KeyShift & (EFI_RIGHT_SHIFT_PRESSED | EFI_LEFT_SHIFT_PRESSED)))
                  ? LV_KEY_PREV : LV_KEY_NEXT;
      break;

    case CHAR_NULL:
      switch (KeyData.Key.ScanCode) {
      case SCAN_UP:        data->key = LV_KEY_UP;        break;
      case SCAN_DOWN:      data->key = LV_KEY_DOWN;      break;
      case SCAN_RIGHT:     data->key = LV_KEY_RIGHT;     break;
      case SCAN_LEFT:      data->key = LV_KEY_LEFT;      break;
      case SCAN_ESC:       data->key = LV_KEY_ESC;       break;
      case SCAN_DELETE:    data->key = LV_KEY_DEL;       break;
      case SCAN_PAGE_DOWN: data->key = LV_KEY_NEXT;      break;
      case SCAN_PAGE_UP:   data->key = LV_KEY_PREV;      break;
      case SCAN_HOME:      data->key = LV_KEY_HOME;      break;
      case SCAN_END:       data->key = LV_KEY_END;       break;
      case SCAN_F1:        data->key = LV_KEY_F1;        break;
      case SCAN_F2:        data->key = LV_KEY_F2;        break;
      case SCAN_F3:        data->key = LV_KEY_F3;        break;
      case SCAN_F4:        data->key = LV_KEY_F4;        break;
      case SCAN_F5:        data->key = LV_KEY_F5;        break;
      case SCAN_F6:        data->key = LV_KEY_F6;        break;
      case SCAN_F7:        data->key = LV_KEY_F7;        break;
      case SCAN_F8:        data->key = LV_KEY_F8;        break;
      case SCAN_F9:        data->key = LV_KEY_F9;        break;
      case SCAN_F10:       data->key = LV_KEY_F10;       break;
      case SCAN_F11:       data->key = LV_KEY_F11;       break;
      case SCAN_F12:       data->key = LV_KEY_F12;       break;
      default:             break;
      }
      break;

    default:
      data->key = KeyData.Key.UnicodeChar;
      break;
    }

    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static lv_indev_t *
lv_uefi_keyboard_create (void)
{
  lv_indev_t *indev = lv_indev_create ();
  if (indev == NULL) {
    return NULL;
  }
  lv_indev_set_type (indev, LV_INDEV_TYPE_KEYPAD);
  lv_indev_set_read_cb (indev, keypad_read);
  return indev;
}

//
// Protocol-install notification: fires each time any driver installs
// EFI_ABSOLUTE_POINTER_PROTOCOL (e.g. UsbMouseAbsolutePointerDxe during BDS).
// Re-grabs mAbsPointer in case it was absent at init.  The live Mode range
// check in mouse_read handles the "range zero until USB fully binds" case
// without further retry here.
//
STATIC
VOID
EFIAPI
OnPointerProtocolInstalled (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mAbsPointer == NULL) {
    gBS->HandleProtocol (
           gST->ConsoleInHandle,
           &gEfiAbsolutePointerProtocolGuid,
           (VOID **)&mAbsPointer);
  }
}

//
// Same as OnPointerProtocolInstalled for EFI_SIMPLE_POINTER_PROTOCOL
// (UsbMouseDxe / Ps2MouseDxe).
//
STATIC
VOID
EFIAPI
OnRelPointerProtocolInstalled (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (mRelPointer == NULL) {
    gBS->HandleProtocol (
           gST->ConsoleInHandle,
           &gEfiSimplePointerProtocolGuid,
           (VOID **)&mRelPointer);
  }
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init (lv_display_t *disp)
{
  EFI_STATUS Status;
  INT32      hor_res;
  INT32      ver_res;

  //
  // Keyboard: custom indev that reads one EFI keystroke per tick and returns
  // PRESSED while the buffer is non-empty, RELEASED when it is empty.
  //
  indev_keypad = lv_uefi_keyboard_create ();

  //
  // Pointer: custom indev that owns the single GetState() call per frame,
  // handling X/Y/buttons and the wheel Z axis together.
  //
  indev_mouse = lv_indev_create ();
  if (indev_mouse != NULL) {
    lv_indev_set_type (indev_mouse, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb (indev_mouse, mouse_read);
    lv_indev_set_display (indev_mouse, disp);

    LV_IMG_DECLARE(mouse_cursor_icon);
    lv_obj_t *cursor_obj = lv_image_create (lv_screen_active ());
    lv_image_set_src (cursor_obj, &mouse_cursor_icon);
    LvglThemeStyleCursor (cursor_obj);
    lv_indev_set_cursor (indev_mouse, cursor_obj);

    //
    // Start hidden; mouse_read() reveals it once a real pointer is detected.
    //
    mCursorObj     = cursor_obj;
    mCursorVisible = FALSE;
    lv_obj_add_flag (mCursorObj, LV_OBJ_FLAG_HIDDEN);

    //
    // Centre the cursor and zero the wheel accumulator.
    //
    hor_res = lv_display_get_horizontal_resolution (disp);
    ver_res = lv_display_get_vertical_resolution (disp);
    mLastCursorX = hor_res / 2;
    mLastCursorY = ver_res / 2;
    mWheelDelta  = 0;
    mLastAbsX    = 0;
    mLastAbsY    = 0;
    mLastAbsZ    = 0;
    mRelRemX     = 0;
    mRelRemY     = 0;
    mRelRemZ     = 0;
    mAbsLeftBtn  = FALSE;
    mRelLeftBtn  = FALSE;
  }

  //
  // Try to grab the pointer protocols now; USB may already be bound.
  //
  gBS->HandleProtocol (
         gST->ConsoleInHandle,
         &gEfiAbsolutePointerProtocolGuid,
         (VOID **)&mAbsPointer);
  gBS->HandleProtocol (
         gST->ConsoleInHandle,
         &gEfiSimplePointerProtocolGuid,
         (VOID **)&mRelPointer);

  //
  // Arm a protocol-install notification so OnPointerProtocolInstalled retries
  // the HandleProtocol lookup once USB binds during BDS (PR#17 pattern).
  //
  if (mPointerNotifyEvent == NULL) {
    Status = gBS->CreateEvent (
                   EVT_NOTIFY_SIGNAL,
                   TPL_CALLBACK,
                   OnPointerProtocolInstalled,
                   NULL,
                   &mPointerNotifyEvent);
    if (!EFI_ERROR (Status)) {
      gBS->RegisterProtocolNotify (
             &gEfiAbsolutePointerProtocolGuid,
             mPointerNotifyEvent,
             &mPointerNotifyRegistration);
    }
  }

  if (mRelNotifyEvent == NULL) {
    Status = gBS->CreateEvent (
                   EVT_NOTIFY_SIGNAL,
                   TPL_CALLBACK,
                   OnRelPointerProtocolInstalled,
                   NULL,
                   &mRelNotifyEvent);
    if (!EFI_ERROR (Status)) {
      gBS->RegisterProtocolNotify (
             &gEfiSimplePointerProtocolGuid,
             mRelNotifyEvent,
             &mRelNotifyRegistration);
    }
  }
}

void lv_uefi_keypad_drain (void)
{
  EFI_STATUS                         Status;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL  *TxtInEx;
  EFI_KEY_DATA                       KeyData;
  lv_indev_t                        *Indev;

  //
  // Drain all pending keystrokes from the EFI console so none leak into the
  // next LVGL event loop iteration.
  //
  Status = gBS->HandleProtocol (
                  gST->ConsoleInHandle,
                  &gEfiSimpleTextInputExProtocolGuid,
                  (VOID **)&TxtInEx);
  if (!EFI_ERROR (Status)) {
    while (!EFI_ERROR (TxtInEx->ReadKeyStrokeEx (TxtInEx, &KeyData))) {
    }
  }

  //
  // Force every keypad indev to RELEASED so LVGL does not interpret a
  // pending ENTER release as a click on the next focused widget.
  //
  Indev = NULL;
  while ((Indev = lv_indev_get_next (Indev)) != NULL) {
    if (lv_indev_get_type (Indev) == LV_INDEV_TYPE_KEYPAD) {
      Indev->keypad.last_state = LV_INDEV_STATE_RELEASED;
      Indev->keypad.last_key   = 0;
    }
  }
}

//
// Report whether a usable pointer (mouse) is present. This tracks the same
// signal used to reveal the cursor: TRUE only once genuine pointer motion or a
// click has been seen, and FALSE again if the pointer device disappears. Lets
// the UI hide mouse-only affordances (e.g. the on-screen keyboard) on machines
// driven purely by a physical keyboard.
//
bool lv_uefi_pointer_is_present (void)
{
  return mCursorVisible;
}

void lv_port_indev_close (void)
{
  if (mPointerNotifyEvent != NULL) {
    gBS->CloseEvent (mPointerNotifyEvent);
    mPointerNotifyEvent        = NULL;
    mPointerNotifyRegistration = NULL;
  }

  if (mRelNotifyEvent != NULL) {
    gBS->CloseEvent (mRelNotifyEvent);
    mRelNotifyEvent        = NULL;
    mRelNotifyRegistration = NULL;
  }

  mAbsPointer  = NULL;
  mRelPointer  = NULL;
  mWheelDelta  = 0;
  mLastAbsZ    = 0;
  mRelRemX     = 0;
  mRelRemY     = 0;
  mRelRemZ     = 0;
  mRelLeftBtn  = FALSE;
  indev_mouse  = NULL;
  indev_keypad = NULL;
}
