## @file
#  DXE firmware-volume integration fragment for LvglPkg.
#
#  Include this file inside the platform's DXE FV section.  The platform must
#  keep the stock DisplayEngineDxe INF: LvglDisplayEngineDxe depends on it.
#
#  Copyright (c) 2026, Hamit Karaca. All rights reserved.<BR>
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

INF  LvglPkg/LvglDisplayEngineDxe/LvglDisplayEngineDxe.inf
INF  LvglPkg/LvglSetupDxe/LvglSetupDxe.inf
