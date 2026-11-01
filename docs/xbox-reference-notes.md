# Original Xbox reference notes (OGBox)

OGBox by Press5elect. These are working notes from xboxdevwiki.net, reverseengineering.wiki, XboxDev/nxdk
and xqemu/xqemu, read 2026-11-01. Most xboxdevwiki pages are short stubs. Where a page doesn't
say something, these notes don't either. The deeper source is still the code: hakuX (HLE),
xemu `hw/xbox/` (LLE, successor of xqemu), Cxbx-Reloaded `src/common/xbe/Xbe.h`.

## Machine
- CPU: Pentium III "Coppermine-128", 733 MHz, 128 KB L2 (SL5SN). xemu uses CPU type `pentium3`.
- NV2A: northbridge + GPU, NV20 family (Kelvin). Revisions XGPU (1.0), XGPU S (1.1-1.4), XGPU-B (1.6).
- MCPX: NVIDIA southbridge: APU + ACI (AC97), USB, PCI, IDE, NIC, SMBus; holds the 512-byte MCPX ROM.
- RAM shared CPU/GPU: retail 64 MB, debug/Chihiro 128 MB. XBE init flag `Limit64Megabytes` (0x4).

## Physical memory map (retail)
| Range | What |
|---|---|
| 0x00000000-0x03FFFFFF | RAM (64 MB; 0x07FFFFFF with 128 MB) |
| 0xF0000000-0xF3FFFFFF | RAM shadow (0xF7FFFFFF with 128 MB) |
| 0xFD000000-0xFDFFFFFF | NV2A registers |
| 0xFE800000-0xFE87FFFF | APU registers |
| 0xFEC00000-0xFEC00FFF | ACI (AC97) |
| 0xFED00000 / 0xFED08000 | USB 0 / USB 1 (OHCI, 4 KB each) |
| 0xFEF00000-0xFEF003FF | NIC (NVNet) |
| 0xFF000000-0xFFFFFFFF | Flash (256 KB image repeated 64x); MCPX ROM overlays the last 512 B until hidden |

I/O ports: PIC/PIT/CMOS in 0x00xx, PCI config 0xCF8-0xCFF, SMBus 0xC000-0xC00F (also 0x1000),
LPC PM 0x8000-0x80FF, NIC 0xE000-0xE007. Debug/Chihiro Super I/O 0x2E/0x2F, serial 0x3F8.

## SMBus (hardware address; software address = hw << 1)
SMC PIC16LC 0x10, EEPROM 0x54, ADM1032 temp 0x4C (not 1.6), Conexant CX25871 0x45, Focus FS454 0x6A
(1.4/1.5), Xcalibur 0x70 (1.6). Controller: 0xC000 status (0 abort, 1 collision, 2 protocol err,
3 busy, 4 complete, 5 timeout), 0xC002 control (2-0 cycle type, 3 start, 4 int enable, 5 abort),
0xC004 address, 0xC006 data, 0xC008 command. SMC: PIC16LC63A at 10 MHz; reboot, AV pack, tray, fan,
LED, temp, power/eject. Revisions P01, P05, P11, P2L (Xyclops), D01, D05.

## Boot
MCPX ROM (1.0 RC4 2BL -> 0x90000; 1.1 TEA-hashed FBL at 0xFFFFD400) -> X-codes at 0xFF000080 -> FBL
(1.1) -> 2BL: MTRRs, page directory at 0xF000, ESP 0x80400000, hides MCPX ROM, SMC challenge, IDE + NIC
on, old 2BL area filled 0xCC, kernel RC4-decrypted + LZX to 0x80010000 -> kernel stage 1-3 (IDT) ->
logo/animation by direct hardware access -> disc check or xboxdash.xbe on partition 3. Flash layout:
NV2A init table 0x0 (magic 0x2B16D065), MCPX table 0x70, X-codes 0x80, 2BL 0x39E00 (0x6000 B),
FBL 0x3D400 (0x2880 B, 4817+), decoy loader 0x3FE00. BIOS versions 3944, 4034, 4134 (MCPX 1.0);
4817, 5101, 5530, 5713, 5838 (MCPX 1.1).

## Kernel
xboxkrnl.exe, image base 0x80010000, identity-mapped sections (.text, .data, STICKY, IDEXPRDT, INIT
discarded). DOS-header patch at offset 40: .data sizes, flash copy and RAM address. Ordinals 1-378.
- Data exports (memory, not stubs): KeInterruptTime 120, KeSystemTime 154, KeTickCount 156,
  KeTimeIncrement 157, LaunchDataPage 164, XboxEEPROMKey 321, XboxHardwareInfo 322, XboxHDKey 323,
  XboxKrnlVersion 324, XboxSignatureKey 325, XeImageFileName 326, XboxLANKey 353,
  XboxAlternateSignatureKeys 354 (325/353/354 change with the running XBE's certificate).
- Fastcall: KfRaiseIrql 160, KfLowerIrql 161, IofCallDriver 86, IofCompleteRequest 87, Interlocked*.
- Time: KeQueryInterruptTime 125, KeQueryPerformanceCounter 126 / Frequency 127, KeQuerySystemTime 128.
- Waits: KeWaitForSingleObject 159, KeWaitForMultipleObjects 158, NtWaitForSingleObject 233,
  NtWaitForSingleObjectEx 234, NtWaitForMultipleObjectsEx 235; NtReleaseMutant (Def Jam spins these).
- Memory: MmAllocateContiguousMemory(Ex) 165/166, MmGetPhysicalAddress 173, MmMapIoSpace 177,
  NtAllocateVirtualMemory 184. Committed pages read as zero (NT); hakuX does this (kernel/mm.c,
  MEM_NOZERO aside).

## XBE
Header: magic XBEH; 0x104 base (0x10000); 0x118 certificate; 0x11C/0x120 sections; 0x124 init flags
(1 mount utility, 2 format utility, 4 limit 64 MB, 8 don't set up HDD); 0x128 entry point XOR retail
0xA8FC57AB / debug 0x94859D4B / beta 0xE682F45B; 0x12C TLS; 0x130 stack size; 0x158 kernel thunk XOR
retail 0x5B6D40B6 / debug 0xEFB1F152 / beta 0x46437DCD (try debug first, then retail); 0x160/0x164
library versions (8-char name, major, minor, build, flags: QFE 0x1FFF, approved 0x6000, debug 0x8000);
0x170/0x174 logo. Thunk table: up to 366 entries, zero-terminated, ordinal | 0x80000000. Title ID =
2 ASCII publisher letters + u16 (EA-023 = 0x45410017). Media flags 0x1 HDD ... 0x80000000 nonsecure.
Section header: SHA-1 over u32 length + data. Chihiro keys: Cxbx-Reloaded Xbe.h.

## Xbox ADPCM
Format tag 0x0069; IMA ADPCM step/index tables; 36-byte blocks mono / 72 stereo; 64 nibbles per channel
per block + the header sample = 65 samples per channel. The page doesn't settle the stereo interleave;
hakuX's APU decoder (ADPCM_SAMPLES_PER_BLOCK) is the reference.

## FATX
Header: "FATX", serial (from RTC), sectors per cluster (32 HDD = 16 KB, 4 MU = 2 KB), root dir clusters
(1), UTF-16 name 0x40 B. The rest: hackmd.io/DXo0p8VnQLy3XQ7YR1Ny_w.

## Test tools
- nxdk (XboxDev): open SDK (pbkit GPU, hal, xboxkrnl import lib, PDCLib, winapi, SDL2, lwIP,
  cxbe, extract-xiso). Small test XBEs could check one behaviour at a time on OGBox: APU voice
  notifications and interrupts (Def Jam), FPU control word and NaN handling (Crash Bandicoot),
  occlusion reports and draw throughput (Burnout 3).
- xqemu: the old QEMU-based LLE emulator; xemu continues it (hw/xbox/, hw/xbox/nv2a/).
