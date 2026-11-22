"""COM interface layouts and GUIDs of the platform APIs THPS2 uses.

Method order and stack arity (arguments after `this`, all stdcall) follow the
DirectX 7 SDK and DirectPlay headers: facts about the public ABI, not code.
Every interface starts with the three IUnknown methods. `PRODUCERS` names the
methods whose out-parameter receives another interface pointer; the type
propagation in `tony.hal.infer` follows them. The arity is also written as a
call-site override during `tony.hal.facts` export, because Ghidra otherwise
guesses caller cleanup for some un-prototyped indirect calls and then
mis-tracks the stack after them.
"""

from __future__ import annotations

import uuid

_IUNKNOWN = "QueryInterface/2 AddRef/0 Release/0"

_LAYOUTS = {
    "IDirectDraw7": """
        Compact/0 CreateClipper/3 CreatePalette/4 CreateSurface/3 DuplicateSurface/2 EnumDisplayModes/4
        EnumSurfaces/4 FlipToGDISurface/0 GetCaps/2 GetDisplayMode/1 GetFourCCCodes/2 GetGDISurface/1
        GetMonitorFrequency/1 GetScanLine/1 GetVerticalBlankStatus/1 Initialize/1 RestoreDisplayMode/0
        SetCooperativeLevel/2 SetDisplayMode/5 WaitForVerticalBlank/2 GetAvailableVidMem/3
        GetSurfaceFromDC/2 RestoreAllSurfaces/0 TestCooperativeLevel/0 GetDeviceIdentifier/2
        StartModeTest/3 EvaluateMode/2""",
    "IDirectDrawSurface7": """
        AddAttachedSurface/1 AddOverlayDirtyRect/1 Blt/5 BltBatch/3 BltFast/5 DeleteAttachedSurface/2
        EnumAttachedSurfaces/2 EnumOverlayZOrders/3 Flip/2 GetAttachedSurface/2 GetBltStatus/1 GetCaps/1
        GetClipper/1 GetColorKey/2 GetDC/1 GetFlipStatus/1 GetOverlayPosition/2 GetPalette/1
        GetPixelFormat/1 GetSurfaceDesc/1 Initialize/2 IsLost/0 Lock/4 ReleaseDC/1 Restore/0 SetClipper/1
        SetColorKey/2 SetOverlayPosition/2 SetPalette/1 Unlock/1 UpdateOverlay/5 UpdateOverlayDisplay/1
        UpdateOverlayZOrder/2 GetDDInterface/1 PageLock/1 PageUnlock/1 SetSurfaceDesc/2 SetPrivateData/4
        GetPrivateData/3 FreePrivateData/1 GetUniquenessValue/1 ChangeUniquenessValue/0 SetPriority/1
        GetPriority/1 SetLOD/1 GetLOD/1""",
    "IDirectDrawPalette": "GetCaps/1 GetEntries/4 Initialize/3 SetEntries/4",
    "IDirectDrawClipper": "GetClipList/3 GetHWnd/1 Initialize/2 IsClipListChanged/1 SetClipList/2 SetHWnd/2",
    "IDirectDrawGammaControl": "GetGammaRamp/2 SetGammaRamp/2",
    "IDirect3D7": "EnumDevices/2 CreateDevice/3 CreateVertexBuffer/3 EnumZBufferFormats/3 EvictManagedTextures/0",
    "IDirect3DDevice7": """
        GetCaps/1 EnumTextureFormats/2 BeginScene/0 EndScene/0 GetDirect3D/1 SetRenderTarget/2
        GetRenderTarget/1 Clear/6 SetTransform/2 GetTransform/2 SetViewport/1 MultiplyTransform/2
        GetViewport/1 SetMaterial/1 GetMaterial/1 SetLight/2 GetLight/2 SetRenderState/2 GetRenderState/2
        BeginStateBlock/0 EndStateBlock/1 PreLoad/1 DrawPrimitive/5 DrawIndexedPrimitive/7 SetClipStatus/1
        GetClipStatus/1 DrawPrimitiveStrided/5 DrawIndexedPrimitiveStrided/7 DrawPrimitiveVB/5
        DrawIndexedPrimitiveVB/7 ComputeSphereVisibility/5 GetTexture/2 SetTexture/2
        GetTextureStageState/3 SetTextureStageState/3 ValidateDevice/1 ApplyStateBlock/1
        CaptureStateBlock/1 DeleteStateBlock/1 CreateStateBlock/2 Load/5 LightEnable/2 GetLightEnable/2
        SetClipPlane/2 GetClipPlane/2 GetInfo/3""",
    "IDirect3DVertexBuffer7": """
        Lock/3 Unlock/0 ProcessVertices/7 GetVertexBufferDesc/1 Optimize/2 ProcessVerticesStrided/7""",
    # DirectInputCreateA(version 0x0700) returns IDirectInputA; 2A/7A extend the layout.
    "IDirectInputA": """
        CreateDevice/3 EnumDevices/4 GetDeviceStatus/1 RunControlPanel/2 Initialize/2 FindDevice/3
        CreateDeviceEx/4""",
    # IDirectInputDeviceA, then the IDirectInputDevice2A and 7A extensions.
    "IDirectInputDeviceA": """
        GetCapabilities/1 EnumObjects/3 GetProperty/2 SetProperty/2 Acquire/0 Unacquire/0 GetDeviceState/2
        GetDeviceData/4 SetDataFormat/1 SetEventNotification/1 SetCooperativeLevel/2 GetObjectInfo/3
        GetDeviceInfo/1 RunControlPanel/2 Initialize/3
        CreateEffect/4 EnumEffects/3 GetEffectInfo/2 GetForceFeedbackState/1 SendForceFeedbackCommand/1
        EnumCreatedEffectObjects/3 Escape/1 Poll/0 SendDeviceData/4
        EnumEffectsInFile/4 WriteEffectToFile/4""",
    "IDirectInputEffect": """
        Initialize/3 GetEffectGuid/1 GetParameters/2 SetParameters/2 Start/2 Stop/0 GetEffectStatus/1
        Download/0 Unload/0 Escape/1""",
    "IDirectSound": """
        CreateSoundBuffer/3 GetCaps/1 DuplicateSoundBuffer/2 SetCooperativeLevel/2 Compact/0
        GetSpeakerConfig/1 SetSpeakerConfig/1 Initialize/1""",
    "IDirectSoundBuffer": """
        GetCaps/1 GetCurrentPosition/2 GetFormat/3 GetVolume/1 GetPan/1 GetFrequency/1 GetStatus/1
        Initialize/2 Lock/7 Play/3 SetCurrentPosition/1 SetFormat/1 SetVolume/1 SetPan/1 SetFrequency/1
        Stop/0 Unlock/4 Restore/0""",
    # IDirectPlay2A, then the IDirectPlay3A and IDirectPlay4A extensions.
    "IDirectPlay4A": """
        AddPlayerToGroup/2 Close/0 CreateGroup/5 CreatePlayer/6 DeletePlayerFromGroup/2 DestroyGroup/1
        DestroyPlayer/1 EnumGroupPlayers/5 EnumGroups/4 EnumPlayers/4 EnumSessions/5 GetCaps/2
        GetGroupData/4 GetGroupName/3 GetMessageCount/2 GetPlayerAddress/3 GetPlayerCaps/3
        GetPlayerData/4 GetPlayerName/3 GetSessionDesc/2 Initialize/1 Open/2 Receive/5 Send/5
        SetGroupData/4 SetGroupName/3 SetPlayerData/4 SetPlayerName/3 SetSessionDesc/2
        AddGroupToGroup/2 CreateGroupInGroup/6 DeleteGroupFromGroup/2 EnumConnections/4
        EnumGroupsInGroup/5 GetGroupConnectionSettings/4 InitializeConnection/2 SecureOpen/4
        SendChatMessage/4 SetGroupConnectionSettings/3 StartSession/2 GetGroupFlags/2 GetGroupParent/2
        GetPlayerAccount/4 GetPlayerFlags/2
        GetGroupOwner/2 SetGroupOwner/2 SendEx/9 GetMessageQueue/5 CancelMessage/2 CancelPriority/3""",
    # IDirectPlayLobbyA, then the Lobby2A and Lobby3A extensions.
    "IDirectPlayLobby3A": """
        Connect/3 CreateAddress/6 EnumAddress/4 EnumAddressTypes/4 EnumLocalApplications/3
        GetConnectionSettings/3 ReceiveLobbyMessage/5 RunApplication/4 SendLobbyMessage/4
        SetConnectionSettings/3 SetLobbyMessageEvent/3
        CreateCompoundAddress/4
        ConnectEx/4 RegisterApplication/2 UnregisterApplication/2 WaitForConnectionSettings/1""",
}


def _methods(layout: str) -> tuple[tuple[str, int], ...]:
    pairs = []
    for token in (_IUNKNOWN + " " + layout).split():
        name, arity = token.split("/")
        pairs.append((name, int(arity)))
    return tuple(pairs)


METHODS: dict[str, tuple[tuple[str, int], ...]] = {name: _methods(layout) for name, layout in _LAYOUTS.items()}
INTERFACES: dict[str, tuple[str, ...]] = {name: tuple(m for m, _ in methods) for name, methods in METHODS.items()}

# Family of each interface: the platform API that owns it.
FAMILY = {
    "IDirectDraw7": "ddraw", "IDirectDrawSurface7": "ddraw", "IDirectDrawPalette": "ddraw",
    "IDirectDrawClipper": "ddraw", "IDirectDrawGammaControl": "ddraw", "IDirect3D7": "d3d",
    "IDirect3DDevice7": "d3d", "IDirect3DVertexBuffer7": "d3d", "IDirectInputA": "dinput",
    "IDirectInputDeviceA": "dinput", "IDirectInputEffect": "dinput", "IDirectSound": "dsound",
    "IDirectSoundBuffer": "dsound", "IDirectPlay4A": "dplay", "IDirectPlayLobby3A": "dplay",
}

GUIDS: dict[str, str] = {
    "IID_IUnknown": "00000000-0000-0000-c000-000000000046",
    "IID_IDirectDraw": "6c14db80-a733-11ce-a521-0020af0be560",
    "IID_IDirectDraw2": "b3a6f3e0-2b43-11cf-a2de-00aa00b93356",
    "IID_IDirectDraw4": "9c59509a-39bd-11d1-8c4a-00c04fd930c5",
    "IID_IDirectDraw7": "15e65ec0-3b9c-11d2-b92f-00609797ea5b",
    "IID_IDirectDrawSurface": "6c14db81-a733-11ce-a521-0020af0be560",
    "IID_IDirectDrawSurface4": "0b2b8630-ad35-11d0-8ea6-00609797ea5b",
    "IID_IDirectDrawSurface7": "06675a80-3b9b-11d2-b92f-00609797ea5b",
    "IID_IDirectDrawPalette": "6c14db84-a733-11ce-a521-0020af0be560",
    "IID_IDirectDrawClipper": "6c14db85-a733-11ce-a521-0020af0be560",
    "IID_IDirectDrawGammaControl": "69c11c3e-b46b-11d1-ad7a-00c04fc29b4e",
    "IID_IDirect3D3": "bb223240-e72b-11d0-a9b4-00aa00c0993e",
    "IID_IDirect3D7": "f5049e77-4861-11d2-a407-00a0c90629a8",
    "IID_IDirect3DRGBDevice": "a4665c60-2673-11cf-a31a-00aa00b93356",
    "IID_IDirect3DHALDevice": "84e63de0-46aa-11cf-816f-0000c020156e",
    "IID_IDirect3DMMXDevice": "881949a1-d6f3-11d0-89ab-00a0c9054129",
    "IID_IDirect3DRefDevice": "50936643-13e9-11d1-89aa-00a0c9054129",
    "IID_IDirect3DNullDevice": "8767df22-bacc-11d1-8969-00a0c90629a8",
    "IID_IDirect3DTnLHalDevice": "f5049e78-4861-11d2-a407-00a0c90629a8",
    "IID_IDirectInputA": "89521360-aa8a-11cf-bfc7-444553540000",
    "IID_IDirectInput2A": "5944e662-aa8a-11cf-bfc7-444553540000",
    "IID_IDirectInput7A": "9a4cb684-236d-11d3-8e9d-00c04f6844ae",
    "IID_IDirectInputDeviceA": "5944e680-c92e-11cf-bfc7-444553540000",
    "IID_IDirectInputDevice2A": "5944e682-c92e-11cf-bfc7-444553540000",
    "IID_IDirectInputDevice7A": "57d7c6bc-2356-11d3-8e9d-00c04f6844ae",
    "IID_IDirectInputEffect": "e7e1f7c0-88d2-11d0-9ad0-00a0c9a06e35",
    "GUID_SysMouse": "6f1d2b60-d5a0-11cf-bfc7-444553540000",
    "GUID_SysKeyboard": "6f1d2b61-d5a0-11cf-bfc7-444553540000",
    "GUID_Joystick": "6f1d2b70-d5a0-11cf-bfc7-444553540000",
    "GUID_XAxis": "a36d02e0-c9f3-11cf-bfc7-444553540000",
    "GUID_YAxis": "a36d02e1-c9f3-11cf-bfc7-444553540000",
    "GUID_ConstantForce": "13541c20-8e33-11d0-9ad0-00a0c9a06e35",
    "GUID_Square": "13541c22-8e33-11d0-9ad0-00a0c9a06e35",
    "GUID_Sine": "13541c23-8e33-11d0-9ad0-00a0c9a06e35",
    "IID_IDirectSound": "279afa83-4981-11ce-a521-0020af0be560",
    "IID_IDirectSoundBuffer": "279afa85-4981-11ce-a521-0020af0be560",
    "IID_IDirectSound3DListener": "279afa84-4981-11ce-a521-0020af0be560",
    "IID_IDirectSound3DBuffer": "279afa86-4981-11ce-a521-0020af0be560",
    "CLSID_DirectPlay": "d1eb6d20-8923-11d0-9d97-00a0c90a43cb",
    "IID_IDirectPlay4A": "0ab1c531-4745-11d1-a7a1-0000f803abfc",
    "IID_IDirectPlay4": "0ab1c530-4745-11d1-a7a1-0000f803abfc",
    "IID_IDirectPlayLobby3A": "2db72491-652c-11d1-a7a8-0000f803abfc",
    "IID_IDirectPlayLobby3": "2db72490-652c-11d1-a7a8-0000f803abfc",
}

# QueryInterface result types by IID (layout supersets share one entry).
IID_INTERFACE = {
    "IID_IDirectDraw7": "IDirectDraw7",
    "IID_IDirectDrawSurface7": "IDirectDrawSurface7",
    "IID_IDirectDrawGammaControl": "IDirectDrawGammaControl",
    "IID_IDirect3D7": "IDirect3D7",
    "IID_IDirectInputA": "IDirectInputA",
    "IID_IDirectInput2A": "IDirectInputA",
    "IID_IDirectInput7A": "IDirectInputA",
    "IID_IDirectInputDeviceA": "IDirectInputDeviceA",
    "IID_IDirectInputDevice2A": "IDirectInputDeviceA",
    "IID_IDirectInputDevice7A": "IDirectInputDeviceA",
    "IID_IDirectSound": "IDirectSound",
    "IID_IDirectSoundBuffer": "IDirectSoundBuffer",
    "IID_IDirectPlay4A": "IDirectPlay4A",
    "IID_IDirectPlayLobby3A": "IDirectPlayLobby3A",
}

# (interface, method) -> (out-parameter index after `this`, result interface,
# or an int: the index of the IID argument that decides it).
PRODUCERS: dict[tuple[str, str], tuple[int, object]] = {
    ("IDirectDraw7", "CreateClipper"): (1, "IDirectDrawClipper"),
    ("IDirectDraw7", "CreatePalette"): (2, "IDirectDrawPalette"),
    ("IDirectDraw7", "CreateSurface"): (1, "IDirectDrawSurface7"),
    ("IDirectDraw7", "DuplicateSurface"): (1, "IDirectDrawSurface7"),
    ("IDirectDraw7", "GetGDISurface"): (0, "IDirectDrawSurface7"),
    ("IDirectDraw7", "GetSurfaceFromDC"): (1, "IDirectDrawSurface7"),
    ("IDirectDrawSurface7", "GetAttachedSurface"): (1, "IDirectDrawSurface7"),
    ("IDirectDrawSurface7", "GetClipper"): (0, "IDirectDrawClipper"),
    ("IDirectDrawSurface7", "GetPalette"): (0, "IDirectDrawPalette"),
    ("IDirectDrawSurface7", "GetDDInterface"): (0, "IDirectDraw7"),
    ("IDirect3D7", "CreateDevice"): (2, "IDirect3DDevice7"),
    ("IDirect3D7", "CreateVertexBuffer"): (1, "IDirect3DVertexBuffer7"),
    ("IDirect3DDevice7", "GetDirect3D"): (0, "IDirect3D7"),
    ("IDirect3DDevice7", "GetRenderTarget"): (0, "IDirectDrawSurface7"),
    ("IDirect3DDevice7", "GetTexture"): (1, "IDirectDrawSurface7"),
    ("IDirectInputA", "CreateDevice"): (1, "IDirectInputDeviceA"),
    ("IDirectInputA", "CreateDeviceEx"): (2, 1),
    ("IDirectInputDeviceA", "CreateEffect"): (2, "IDirectInputEffect"),
    ("IDirectSound", "CreateSoundBuffer"): (1, "IDirectSoundBuffer"),
    ("IDirectSound", "DuplicateSoundBuffer"): (1, "IDirectSoundBuffer"),
    ("IDirectPlayLobby3A", "Connect"): (1, "IDirectPlay4A"),  # IDirectPlay2A: the 4A layout's prefix
    ("IDirectPlayLobby3A", "ConnectEx"): (2, 1),
}

# Import factories: 'DLL!export' -> (out-parameter index, result interface or IID argument index).
FACTORIES: dict[str, tuple[int, object]] = {
    "DDRAW.DLL!DirectDrawCreateEx": (1, 2),
    "DINPUT.DLL!DirectInputCreateA": (2, "IDirectInputA"),
    "DSOUND.DLL!Ordinal_1": (1, "IDirectSound"),
    "DPLAYX.DLL!Ordinal_4": (1, "IDirectPlayLobby3A"),
    "OLE32.DLL!CoCreateInstance": (4, 3),
}

# Platform -> game callbacks: (callback argument, context argument or None,
# callback parameter receiving the context or None). COM indices count after
# `this`; import indices are plain argument positions.
CALLBACKS: dict[object, tuple[int, int | None, int | None]] = {
    ("IDirectDraw7", "EnumDisplayModes"): (3, 2, 1),
    ("IDirectDraw7", "EnumSurfaces"): (3, 2, 2),
    ("IDirectDrawSurface7", "EnumAttachedSurfaces"): (1, 0, 2),
    ("IDirectDrawSurface7", "EnumOverlayZOrders"): (2, 1, 2),
    ("IDirect3D7", "EnumDevices"): (0, 1, 3),
    ("IDirect3D7", "EnumZBufferFormats"): (1, 2, 1),
    ("IDirect3DDevice7", "EnumTextureFormats"): (0, 1, 1),
    ("IDirectInputA", "EnumDevices"): (1, 2, 1),
    ("IDirectInputDeviceA", "EnumObjects"): (0, 1, 1),
    ("IDirectInputDeviceA", "EnumEffects"): (0, 1, 1),
    ("IDirectInputDeviceA", "EnumCreatedEffectObjects"): (0, 1, 1),
    ("IDirectInputDeviceA", "EnumEffectsInFile"): (1, 2, 1),
    ("IDirectPlay4A", "EnumSessions"): (2, 3, 3),
    ("IDirectPlay4A", "EnumPlayers"): (1, 2, 4),
    ("IDirectPlay4A", "EnumGroups"): (1, 2, 4),
    ("IDirectPlay4A", "EnumGroupPlayers"): (2, 3, 4),
    ("IDirectPlay4A", "EnumConnections"): (1, 2, 5),
    ("IDirectPlay4A", "EnumGroupsInGroup"): (2, 3, 4),
    ("IDirectPlayLobby3A", "EnumAddress"): (0, 3, 3),
    ("IDirectPlayLobby3A", "EnumAddressTypes"): (0, 2, 1),
    ("IDirectPlayLobby3A", "EnumLocalApplications"): (0, 1, 1),
    "DDRAW.DLL!DirectDrawEnumerateExA": (0, 1, 3),
    "WINMM.DLL!timeSetEvent": (2, 3, 2),
    "USER32.DLL!SetTimer": (3, None, None),
    "KERNEL32.DLL!CreateThread": (2, 3, 0),
    "USER32.DLL!DialogBoxParamA": (3, None, None),
    "USER32.DLL!CreateDialogParamA": (3, None, None),
}

ORDINAL_NAMES = {
    "DSOUND.DLL!Ordinal_1": "DirectSoundCreate",
    "DPLAYX.DLL!Ordinal_4": "DirectPlayLobbyCreateA",
}


def method(interface: str, offset: int) -> str:
    methods = INTERFACES[interface]
    index = offset // 4
    if offset % 4 or index >= len(methods):
        return f"<vtable+{offset:#x}>"
    return methods[index]


def arity(interface: str, offset: int) -> int | None:
    """Stack arguments after `this`, or None for an offset outside the vtable."""

    methods = METHODS[interface]
    index = offset // 4
    if offset % 4 or index >= len(methods):
        return None
    return methods[index][1]


def api_name(api: str) -> str:
    """'DLL!export' with ordinals resolved to their documented names."""

    return ORDINAL_NAMES.get(api, api.split("!", 1)[1])


def guid_bytes(text: str) -> bytes:
    return uuid.UUID(text).bytes_le


def guid_name(data: bytes) -> str | None:
    for name, text in GUIDS.items():
        if guid_bytes(text) == data:
            return name
    return None
