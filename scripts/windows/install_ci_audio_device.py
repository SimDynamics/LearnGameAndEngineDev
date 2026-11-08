#!/usr/bin/env python3
"""Evaluate the original VB-CABLE 46 driver on a disposable hosted Windows VM.

The caller verifies the ZIP and the kernel catalog with SignTool before this
helper runs. This helper pins the extracted package again and uses Windows'
normal driver installation/signature checks. It never runs the vendor installer,
forces a driver, changes certificate stores/security policy, or reboots Windows.

A successful installation is not evidence of an available audio endpoint. The
caller must inspect endpoints and run the real player tests separately. On an
installation failure, leave the new devnode for diagnostics on the disposable VM;
do not remove devices or attempt another installation strategy.

Microsoft API contracts:
https://learn.microsoft.com/windows/win32/api/setupapi/nf-setupapi-setupdicreatedeviceinfow
https://learn.microsoft.com/windows/win32/api/setupapi/nf-setupapi-setupdicallclassinstaller
https://learn.microsoft.com/windows/win32/api/newdev/nf-newdev-updatedriverforplugandplaydevicesw
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import stat
import sys
import uuid


HARDWARE_ID = "VBAudioVACWDM"
INF_NAME = "vbMmeCable64_win10.inf"
MEDIA_CLASS = uuid.UUID("4d36e96c-e325-11ce-bfc1-08002be10318")
# Original files from https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack46.zip
# ZIP SHA-256: bc44e8974191a5a2427d6c979e2dab08dbb09925f3f435dfa2f0ba3e45b6eb90
PACKAGE_FILES = {
    INF_NAME: (11361, "cecf9181b6c85842d707330efc594824a749a1d9e01db46eba0e60e72f0a66ee"),
    "vbaudio_cable64_win10.sys": (
        144632, "7ebcbbe7004d185f7b29613cd27fdc2dd71c1712224b767a74d2f66bdc33a7ef"
    ),
    "vbaudio_cable64arm_win10.sys": (
        152824, "85c217af826c2e1d478b52afc7c2248889c8478030d44e8134bd4c23b8915216"
    ),
    "vbaudio_cable64_win10.cat": (
        12993, "5d24c17858b49ad83bb856c66c24c5c2aecdb89cbaae2f8179c15d351124fa9f"
    ),
}

# Fixed Windows ABI widths also make structural inspection possible off Windows.
DWORD = ctypes.c_uint32
BOOL = ctypes.c_int32
BYTE = ctypes.c_uint8
HANDLE = ctypes.c_void_p


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", DWORD),
        ("Data2", ctypes.c_uint16),
        ("Data3", ctypes.c_uint16),
        ("Data4", BYTE * 8),
    ]


class SP_DEVINFO_DATA(ctypes.Structure):
    _fields_ = [
        ("cbSize", DWORD),
        ("ClassGuid", GUID),
        ("DevInst", DWORD),
        ("Reserved", ctypes.c_size_t),
    ]


class SP_DEVINSTALL_PARAMS_W(ctypes.Structure):
    _fields_ = [
        ("cbSize", DWORD),
        ("Flags", DWORD),
        ("FlagsEx", DWORD),
        ("hwndParent", HANDLE),
        ("InstallMsgHandler", HANDLE),
        ("InstallMsgHandlerContext", HANDLE),
        ("FileQueue", HANDLE),
        ("ClassInstallReserved", ctypes.c_size_t),
        ("Reserved", DWORD),
        ("DriverPath", ctypes.c_uint16 * 260),
    ]


DIGCF_ALLCLASSES = 0x00000004
DICD_GENERATE_ID = 0x00000001
SPDRP_HARDWAREID = 0x00000001
DIF_REGISTERDEVICE = 0x00000019
DI_NEEDRESTART = 0x00000080
DI_NEEDREBOOT = 0x00000100
DI_QUIETINSTALL = 0x00800000
INSTALLFLAG_NONINTERACTIVE = 0x00000004
ERROR_INVALID_DATA = 13
ERROR_INSUFFICIENT_BUFFER = 122
ERROR_NO_MORE_ITEMS = 259
REG_MULTI_SZ = 7
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


def emit(event: str, **values: object) -> None:
    print(json.dumps({"event": event, **values}, ensure_ascii=True), flush=True)


def require_ci_environment() -> None:
    if sys.platform != "win32" or os.name != "nt":
        raise RuntimeError("This experiment requires Windows.")
    if (os.environ.get("GITHUB_ACTIONS") != "true"
            or os.environ.get("RUNNER_ENVIRONMENT") != "github-hosted"):
        raise RuntimeError("Only a disposable GitHub-hosted Actions runner is permitted.")
    if ctypes.sizeof(HANDLE) != 8 or platform.machine().casefold() not in ("amd64", "x86_64"):
        raise RuntimeError("This experiment requires native x64 Windows and x64 Python.")
    if ctypes.sizeof(SP_DEVINFO_DATA) != 32 or ctypes.sizeof(SP_DEVINSTALL_PARAMS_W) != 584:
        raise RuntimeError("Unexpected Windows x64 SetupAPI structure layout.")


def require_physical_path(path: Path) -> None:
    for component in (path, *path.parents):
        info = component.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise RuntimeError(f"Links and reparse points are not permitted: {component}")


def verify_driver_package(directory: Path) -> Path:
    directory = directory.absolute()
    if ".." in directory.parts or str(directory).startswith("\\\\"):
        raise RuntimeError("Use an ordinary local directory without parent traversals.")
    require_physical_path(directory)
    if not directory.is_dir():
        raise RuntimeError(f"Driver directory does not exist: {directory}")
    directory = directory.resolve(strict=True)
    for name, (expected_size, expected_hash) in PACKAGE_FILES.items():
        path = directory / name
        require_physical_path(path)
        info = path.stat()
        if not stat.S_ISREG(info.st_mode) or info.st_size != expected_size:
            raise RuntimeError(f"Unexpected original package file/size: {path}")
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            raise RuntimeError(f"Original package SHA-256 mismatch: {path}")
        emit("package_file_verified", file=name, sha256=actual_hash)
    inf = directory / INF_NAME
    if len(str(inf)) >= 260:
        raise RuntimeError("The absolute INF path exceeds SetupAPI's MAX_PATH contract.")
    return inf


class SetupApi:
    def __init__(self) -> None:
        # Load only Windows system DLLs, never a DLL next to the extracted package.
        self.setup = ctypes.WinDLL("setupapi.dll", use_last_error=True, winmode=0x00000800)
        self.newdev = ctypes.WinDLL("newdev.dll", use_last_error=True, winmode=0x00000800)
        pointer = ctypes.POINTER
        dev = pointer(SP_DEVINFO_DATA)
        guid = pointer(GUID)
        dword = pointer(DWORD)
        string = ctypes.c_wchar_p
        byte_buffer = pointer(BYTE)
        params = pointer(SP_DEVINSTALL_PARAMS_W)
        bindings = {
            "SetupDiGetClassDevsW": (HANDLE, [guid, string, HANDLE, DWORD]),
            "SetupDiEnumDeviceInfo": (BOOL, [HANDLE, DWORD, dev]),
            "SetupDiGetDeviceRegistryPropertyW": (
                BOOL, [HANDLE, dev, DWORD, dword, byte_buffer, DWORD, dword]
            ),
            "SetupDiDestroyDeviceInfoList": (BOOL, [HANDLE]),
            "SetupDiGetINFClassW": (BOOL, [string, guid, string, DWORD, dword]),
            "SetupDiCreateDeviceInfoList": (HANDLE, [guid, HANDLE]),
            "SetupDiCreateDeviceInfoW": (BOOL, [HANDLE, string, guid, string, HANDLE, DWORD, dev]),
            "SetupDiSetDeviceRegistryPropertyW": (BOOL, [HANDLE, dev, DWORD, byte_buffer, DWORD]),
            "SetupDiGetDeviceInstallParamsW": (BOOL, [HANDLE, dev, params]),
            "SetupDiSetDeviceInstallParamsW": (BOOL, [HANDLE, dev, params]),
            "SetupDiCallClassInstaller": (BOOL, [DWORD, HANDLE, dev]),
            "SetupDiGetDeviceInstanceIdW": (BOOL, [HANDLE, dev, string, DWORD, dword]),
        }
        for name, (result, arguments) in bindings.items():
            function = getattr(self.setup, name)
            function.restype = result
            function.argtypes = arguments
        self.newdev.UpdateDriverForPlugAndPlayDevicesW.restype = BOOL
        self.newdev.UpdateDriverForPlugAndPlayDevicesW.argtypes = [
            HANDLE, string, string, DWORD, pointer(BOOL)
        ]

    @staticmethod
    def error(operation: str, code: int | None = None) -> RuntimeError:
        if code is None:
            code = ctypes.get_last_error()
        code &= 0xFFFFFFFF
        return RuntimeError(f"{operation} failed: 0x{code:08X} ({code}): {ctypes.FormatError(code).strip()}")

    def checked(self, operation: str, *arguments: object) -> None:
        if not getattr(self.setup, operation)(*arguments):
            raise self.error(operation)

    @contextmanager
    def device_set(self, handle: int):
        if handle is None or handle == INVALID_HANDLE_VALUE:
            raise self.error("Create/enumerate device information set")
        try:
            yield handle
        finally:
            # This releases a handle; it does not remove the registered device.
            if not self.setup.SetupDiDestroyDeviceInfoList(handle):
                error = self.error("SetupDiDestroyDeviceInfoList")
                if sys.exc_info()[0] is None:
                    raise error
                emit("device_info_handle_release_failed", error=str(error))

    @staticmethod
    def device_info() -> SP_DEVINFO_DATA:
        info = SP_DEVINFO_DATA()
        info.cbSize = ctypes.sizeof(info)
        return info

    def instance_id(self, handle: int, info: SP_DEVINFO_DATA) -> str:
        buffer = ctypes.create_unicode_buffer(512)
        self.checked("SetupDiGetDeviceInstanceIdW", handle, ctypes.byref(info), buffer, len(buffer), None)
        return buffer.value

    def hardware_ids(self, handle: int, info: SP_DEVINFO_DATA) -> list[str]:
        kind, required = DWORD(), DWORD()
        result = self.setup.SetupDiGetDeviceRegistryPropertyW(
            handle, ctypes.byref(info), SPDRP_HARDWAREID, ctypes.byref(kind), None, 0, ctypes.byref(required)
        )
        if not result:
            code = ctypes.get_last_error()
            if code == ERROR_INVALID_DATA:
                return []  # Some device classes have no hardware-ID property.
            if code != ERROR_INSUFFICIENT_BUFFER:
                raise self.error("Query hardware-ID size", code)
        if required.value < 4 or required.value > 1024 * 1024 or required.value % 2:
            raise RuntimeError("Invalid hardware-ID REG_MULTI_SZ size.")
        buffer = (BYTE * required.value)()
        self.checked(
            "SetupDiGetDeviceRegistryPropertyW", handle, ctypes.byref(info), SPDRP_HARDWAREID,
            ctypes.byref(kind), buffer, len(buffer), ctypes.byref(required)
        )
        if kind.value != REG_MULTI_SZ or required.value > len(buffer) or required.value % 2:
            raise RuntimeError("Invalid hardware-ID registry property.")
        text = bytes(buffer[:required.value]).decode("utf-16-le")
        if not text.endswith("\0\0"):
            raise RuntimeError("Unterminated hardware-ID REG_MULTI_SZ.")
        return [value for value in text[:-2].split("\0") if value]

    def matching_devices(self) -> list[str]:
        matches = []
        # Include non-present devnodes: an old instance must also prevent a retry.
        handle = self.setup.SetupDiGetClassDevsW(None, None, None, DIGCF_ALLCLASSES)
        with self.device_set(handle):
            index = 0
            while True:
                info = self.device_info()
                if not self.setup.SetupDiEnumDeviceInfo(handle, index, ctypes.byref(info)):
                    code = ctypes.get_last_error()
                    if code == ERROR_NO_MORE_ITEMS:
                        break
                    raise self.error("SetupDiEnumDeviceInfo", code)
                ids = self.hardware_ids(handle, info)
                if any(value.casefold() == HARDWARE_ID.casefold() for value in ids):
                    matches.append(self.instance_id(handle, info))
                index += 1
        return matches

    def install_params(self, handle: int, info: SP_DEVINFO_DATA) -> SP_DEVINSTALL_PARAMS_W:
        params = SP_DEVINSTALL_PARAMS_W()
        params.cbSize = ctypes.sizeof(params)
        self.checked("SetupDiGetDeviceInstallParamsW", handle, ctypes.byref(info), ctypes.byref(params))
        return params

    @staticmethod
    def require_no_restart(params: SP_DEVINSTALL_PARAMS_W, operation: str, required: bool = False) -> None:
        if required or params.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT):
            emit("restart_required", operation=operation, needsReboot=True, flags=params.Flags, endpointVerified=False)
            raise RuntimeError(f"{operation} requires a Windows restart; audio verification cannot proceed.")

    def install(self, inf: Path) -> None:
        existing = self.matching_devices()
        if existing:
            raise RuntimeError(f"Refusing to modify existing {HARDWARE_ID} devices: {existing}")

        guid, class_name = GUID(), ctypes.create_unicode_buffer(256)
        self.checked("SetupDiGetINFClassW", str(inf), ctypes.byref(guid), class_name, len(class_name), None)
        if bytes(guid) != MEDIA_CLASS.bytes_le or class_name.value.casefold() != "media":
            raise RuntimeError("The authenticated INF did not resolve to the expected MEDIA class.")

        handle = self.setup.SetupDiCreateDeviceInfoList(ctypes.byref(guid), None)
        with self.device_set(handle):
            info = self.device_info()
            self.checked(
                "SetupDiCreateDeviceInfoW", handle, HARDWARE_ID, ctypes.byref(guid),
                "VB-Audio Virtual Cable", None, DICD_GENERATE_ID, ctypes.byref(info)
            )
            ids = (HARDWARE_ID + "\0\0").encode("utf-16-le")
            id_buffer = (BYTE * len(ids)).from_buffer_copy(ids)
            self.checked("SetupDiSetDeviceRegistryPropertyW", handle, ctypes.byref(info), SPDRP_HARDWAREID, id_buffer, len(ids))
            params = self.install_params(handle, info)
            params.Flags |= DI_QUIETINSTALL
            self.checked("SetupDiSetDeviceInstallParamsW", handle, ctypes.byref(info), ctypes.byref(params))
            self.checked("SetupDiCallClassInstaller", DIF_REGISTERDEVICE, handle, ctypes.byref(info))
            instance = self.instance_id(handle, info)
            emit("root_device_registered", instanceId=instance, hardwareId=HARDWARE_ID, endpointVerified=False)
            self.require_no_restart(self.install_params(handle, info), "DIF_REGISTERDEVICE")

            # UpdateDriver acts on every matching devnode. Refuse concurrent or
            # unexpected matches rather than updating any device but our new one.
            if self.matching_devices() != [instance]:
                raise RuntimeError("The hardware-ID match set changed after ROOT registration.")
            reboot = BOOL(0)
            result = self.newdev.UpdateDriverForPlugAndPlayDevicesW(
                None, HARDWARE_ID, str(inf), INSTALLFLAG_NONINTERACTIVE, ctypes.byref(reboot)
            )
            if not result:
                raise self.error("UpdateDriverForPlugAndPlayDevicesW")
            self.require_no_restart(self.install_params(handle, info), "UpdateDriverForPlugAndPlayDevicesW", bool(reboot.value))
            emit(
                "driver_installation_succeeded", instanceId=instance, hardwareId=HARDWARE_ID,
                needsReboot=False, endpointVerified=False
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver-directory", type=Path, required=True)
    args = parser.parse_args()
    try:
        require_ci_environment()
        inf = verify_driver_package(args.driver_directory)
        SetupApi().install(inf)
        return 0
    except (OSError, RuntimeError, UnicodeError) as error:
        emit("driver_installation_failed", error=str(error), endpointVerified=False)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
