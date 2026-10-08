/** @file
  Screenshot to USB storage -- saves the whole screen as a 24-bit BMP file.

  Copyright (c) 2026, LeoHsiao83. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "LvglFormRenderer.h"
#include <Library/BmpSupportLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>

//
// Same-second names get _1 .. _99.
//
#define SCREENSHOT_SUFFIX_MAX  99

/**
  Check whether the file system on @a Handle sits on a USB device.
**/
STATIC
BOOLEAN
IsUsbVolume (
  IN EFI_HANDLE  Handle
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  Node = DevicePathFromHandle (Handle);
  if (Node == NULL) {
    return FALSE;
  }

  for ( ; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == MESSAGING_DEVICE_PATH) &&
        ((DevicePathSubType (Node) == MSG_USB_DP) ||
         (DevicePathSubType (Node) == MSG_USB_CLASS_DP)))
    {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Read the whole screen of the display LVGL draws on into a BMP image.

  @param[out] Bmp      Allocated BMP image; free with FreePool.
  @param[out] BmpSize  Size of @a Bmp in bytes.
**/
STATIC
EFI_STATUS
CaptureScreen (
  OUT VOID    **Bmp,
  OUT UINT32  *BmpSize
  )
{
  EFI_STATUS                     Status;
  EFI_HANDLE                     GopHandle;
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Blt;
  UINT32                         Width;
  UINT32                         Height;

  *Bmp     = NULL;
  *BmpSize = 0;

  GopHandle = lv_uefi_display_get_any ();
  if (GopHandle == NULL) {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->HandleProtocol (GopHandle, &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Width  = Gop->Mode->Info->HorizontalResolution;
  Height = Gop->Mode->Info->VerticalResolution;
  Blt    = AllocatePool ((UINTN)Width * Height * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
  if (Blt == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = Gop->Blt (Gop, Blt, EfiBltVideoToBltBuffer, 0, 0, 0, 0, Width, Height, 0);
  if (!EFI_ERROR (Status)) {
    Status = TranslateGopBltToBmp (Blt, Height, Width, Bmp, BmpSize);
  }

  FreePool (Blt);
  return Status;
}

/**
  Create a new SETUP_YYYYMMDD_HHMMSS[_n].BMP in the root of @a Root.
**/
STATIC
EFI_STATUS
CreateScreenshotFile (
  IN  EFI_FILE_PROTOCOL  *Root,
  OUT EFI_FILE_PROTOCOL  **File,
  OUT CHAR16             *FileName,
  IN  UINTN              FileNameSize
  )
{
  EFI_STATUS         Status;
  EFI_TIME           Time;
  EFI_FILE_PROTOCOL  *Existing;
  UINTN              Suffix;

  if (EFI_ERROR (gRT->GetTime (&Time, NULL))) {
    ZeroMem (&Time, sizeof (Time));
  }

  for (Suffix = 0; Suffix <= SCREENSHOT_SUFFIX_MAX; Suffix++) {
    if (Suffix == 0) {
      UnicodeSPrint (
        FileName,
        FileNameSize,
        L"SETUP_%04d%02d%02d_%02d%02d%02d.BMP",
        Time.Year,
        Time.Month,
        Time.Day,
        Time.Hour,
        Time.Minute,
        Time.Second
        );
    } else {
      UnicodeSPrint (
        FileName,
        FileNameSize,
        L"SETUP_%04d%02d%02d_%02d%02d%02d_%d.BMP",
        Time.Year,
        Time.Month,
        Time.Day,
        Time.Hour,
        Time.Minute,
        Time.Second,
        (UINT32)Suffix
        );
    }

    Status = Root->Open (Root, &Existing, FileName, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR (Status)) {
      Existing->Close (Existing);
      continue;
    }

    return Root->Open (
                   Root,
                   File,
                   FileName,
                   EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                   0
                   );
  }

  return EFI_ACCESS_DENIED;
}

/**
  Save the screen as a BMP file in the root of the first writable USB volume.
  The caller refreshes the screen first and shows the result afterwards.

  @param[out] FileName      Name of the saved file.
  @param[in]  FileNameSize  Size of @a FileName in bytes.
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
  )
{
  EFI_STATUS                       Status;
  EFI_HANDLE                       *Handles;
  UINTN                            HandleCount;
  UINTN                            Index;
  UINTN                            UsbCount;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;
  VOID                             *Bmp;
  UINT32                           BmpSize;
  UINTN                            WriteSize;

  FileName[0] = L'\0';
  *FileSize   = 0;

  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  UsbCount = 0;
  for (Index = 0; Index < HandleCount; Index++) {
    if (IsUsbVolume (Handles[Index])) {
      Handles[UsbCount++] = Handles[Index];
    }
  }

  if (UsbCount == 0) {
    FreePool (Handles);
    return EFI_NOT_FOUND;
  }

  Status = CaptureScreen (&Bmp, &BmpSize);
  if (EFI_ERROR (Status)) {
    FreePool (Handles);
    return Status;
  }

  //
  // A volume counts as writable when the new file can be created on it.
  //
  for (Index = 0; Index < UsbCount; Index++) {
    Status = gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = Fs->OpenVolume (Fs, &Root);
    if (EFI_ERROR (Status)) {
      continue;
    }

    Status = CreateScreenshotFile (Root, &File, FileName, FileNameSize);
    if (EFI_ERROR (Status)) {
      Root->Close (Root);
      continue;
    }

    WriteSize = BmpSize;
    Status    = File->Write (File, &WriteSize, Bmp);
    if (!EFI_ERROR (Status) && (WriteSize != BmpSize)) {
      Status = EFI_VOLUME_FULL;
    }

    if (!EFI_ERROR (Status)) {
      Status = File->Flush (File);
    }

    if (EFI_ERROR (Status)) {
      File->Delete (File);
    } else {
      File->Close (File);
      *FileSize = BmpSize;
    }

    Root->Close (Root);
    break;
  }

  FreePool (Bmp);
  FreePool (Handles);
  return Status;
}
