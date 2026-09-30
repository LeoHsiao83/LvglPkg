## @file
#  Platform DSC integration fragment for LvglPkg.
#
#  Include this file at DSC top level.  The platform must keep its stock
#  DisplayEngineDxe component: LvglDisplayEngineDxe depends on it and patches
#  its protocol instances.
#
#  Copyright (c) 2026, Hamit Karaca. All rights reserved.<BR>
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

[LibraryClasses]
  LvglLib|LvglPkg/Library/LvglLib/LvglLib.inf
  LvglThemeLib|LvglPkg/Library/LvglThemeLib/LvglThemeLib.inf
  LvglUiConfigLib|LvglPkg/Library/LvglUiConfigLib/LvglUiConfigLib.inf

[Components]
  LvglPkg/LvglDisplayEngineDxe/LvglDisplayEngineDxe.inf
  LvglPkg/LvglSetupDxe/LvglSetupDxe.inf
