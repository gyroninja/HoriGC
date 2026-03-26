/*
 * hori_gc_test.c
 *
 * Decompilation of HoriGC_Trimmed.SFC
 * SNES GameCube Controller Tester Cartridge by Hori
 *
 * ROM layout: LoROM, 512KB
 *   Bank 0x00–0x03: Program code (mapped to 0x8000–0xFFFF per bank)
 *   Bank 0x01–0x02: Tile graphics (4bpp/2bpp GC controller images)
 *   Bank 0x02–0x03: Tilemap data / background assets
 *
 * CPU: WDC 65C816 in native mode
 * Reset vector: 0x8000 → Main_Init
 *
 * Overview:
 *   Reads a GameCube controller via bit-bang on JOYSER0 (0x4016).
 *   Displays all button states and analog axis values on screen.
 *   Runs a guided test sequence: connect → calibrate → test all inputs.
 */

#include <stdint.h>

/* =========================================================================
 * SNES hardware register addresses (memory-mapped I/O)
 * ========================================================================= */

/* PPU registers */
#define REG_INIDISP     (*((volatile uint8_t*)0x2100))  /* Screen display (brightness + blanking) */
#define REG_OBSEL       (*((volatile uint8_t*)0x2101))  /* OBJ size and chr base */
#define REG_OAMADDL     (*((volatile uint8_t*)0x2102))  /* OAM address low */
#define REG_OAMADDH     (*((volatile uint8_t*)0x2103))  /* OAM address high */
#define REG_OAMDATA     (*((volatile uint8_t*)0x2104))  /* OAM data write */
#define REG_BGMODE      (*((volatile uint8_t*)0x2105))  /* BG mode and chr size */
#define REG_MOSAIC      (*((volatile uint8_t*)0x2106))  /* Mosaic */
#define REG_BG1SC       (*((volatile uint8_t*)0x2107))  /* BG1 tilemap base/size */
#define REG_BG2SC       (*((volatile uint8_t*)0x2108))  /* BG2 tilemap base/size */
#define REG_BG3SC       (*((volatile uint8_t*)0x2109))  /* BG3 tilemap base/size */
#define REG_BG4SC       (*((volatile uint8_t*)0x210A))  /* BG4 tilemap base/size */
#define REG_BG12NBA     (*((volatile uint8_t*)0x210B))  /* BG1/2 chr base */
#define REG_BG34NBA     (*((volatile uint8_t*)0x210C))  /* BG3/4 chr base */
#define REG_BG1HOFS     (*((volatile uint16_t*)0x210D)) /* BG1 horizontal scroll (write twice) */
#define REG_BG1VOFS     (*((volatile uint16_t*)0x210E)) /* BG1 vertical scroll */
#define REG_BG2HOFS     (*((volatile uint16_t*)0x210F)) /* BG2 horizontal scroll */
#define REG_BG2VOFS     (*((volatile uint16_t*)0x2110)) /* BG2 vertical scroll */
#define REG_BG3HOFS     (*((volatile uint16_t*)0x2111)) /* BG3 horizontal scroll */
#define REG_BG3VOFS     (*((volatile uint16_t*)0x2112)) /* BG3 vertical scroll */
#define REG_BG4HOFS     (*((volatile uint16_t*)0x2113)) /* BG4 horizontal scroll */
#define REG_BG4VOFS     (*((volatile uint16_t*)0x2114)) /* BG4 vertical scroll */
#define REG_VMAIN       (*((volatile uint8_t*)0x2115))  /* VRAM address increment mode */
#define REG_VMADDL      (*((volatile uint8_t*)0x2116))  /* VRAM address low */
#define REG_VMADDH      (*((volatile uint8_t*)0x2117))  /* VRAM address high */
#define REG_VMADD       (*((volatile uint16_t*)0x2116)) /* VRAM address (16-bit) */
#define REG_VMDATAL     (*((volatile uint8_t*)0x2118))  /* VRAM data write low */
#define REG_VMDATAH     (*((volatile uint8_t*)0x2119))  /* VRAM data write high */
#define REG_CGADD       (*((volatile uint8_t*)0x2121))  /* CGRAM (palette) address */
#define REG_CGDATA      (*((volatile uint8_t*)0x2122))  /* CGRAM data write */
#define REG_TM          (*((volatile uint8_t*)0x212C))  /* Main screen layer enable */
#define REG_TS          (*((volatile uint8_t*)0x212D))  /* Sub screen layer enable */
#define REG_COLDATA     (*((volatile uint8_t*)0x2132))  /* Color math data */
#define REG_SETINI      (*((volatile uint8_t*)0x2133))  /* Screen init settings */
#define REG_WOBJSEL     (*((volatile uint8_t*)0x2141))  /* Color math sub screen enable */

/* APU (SPC700) communication ports */
#define REG_APUIO0      (*((volatile uint16_t*)0x2140)) /* APU port 0/1 (16-bit access) */
#define REG_APUIO2      (*((volatile uint16_t*)0x2142)) /* APU port 2/3 (16-bit access) */
#define REG_APUIO0_B    (*((volatile uint8_t*)0x2140))  /* APU port 0 */
#define REG_APUIO1_B    (*((volatile uint8_t*)0x2141))  /* APU port 1 */
#define REG_APUIO2_B    (*((volatile uint8_t*)0x2142))  /* APU port 2/3 low */

/* WRAM access */
#define REG_WMDATA      (*((volatile uint8_t*)0x2180))  /* WRAM data port */
#define REG_WMADDL      (*((volatile uint8_t*)0x2181))  /* WRAM address low */
#define REG_WMADDM      (*((volatile uint8_t*)0x2182))  /* WRAM address mid */
#define REG_WMADDH      (*((volatile uint8_t*)0x2183))  /* WRAM address bank */

/* DMA/HDMA registers (channel 0) */
#define REG_MDMAEN      (*((volatile uint8_t*)0x420B))  /* DMA enable */
#define REG_DMAP0       (*((volatile uint8_t*)0x4300))  /* DMA channel 0 parameters */
#define REG_BBAD0       (*((volatile uint8_t*)0x4301))  /* DMA channel 0 B-bus address */
#define REG_A1T0L       (*((volatile uint8_t*)0x4302))  /* DMA source address low */
#define REG_A1T0H       (*((volatile uint8_t*)0x4303))  /* DMA source address high */
#define REG_A1T0        (*((volatile uint16_t*)0x4302)) /* DMA source address (16-bit) */
#define REG_A1B0        (*((volatile uint8_t*)0x4304))  /* DMA source bank */
#define REG_DAS0L       (*((volatile uint8_t*)0x4305))  /* DMA byte count low */
#define REG_DAS0H       (*((volatile uint8_t*)0x4306))  /* DMA byte count high */
#define REG_DAS0        (*((volatile uint16_t*)0x4305)) /* DMA byte count (16-bit) */

/* I/O ports */
#define REG_NMITIMEN    (*((volatile uint8_t*)0x4200))  /* NMI/IRQ/auto-joypad enable */
#define REG_RDNMI       (*((volatile uint8_t*)0x4210))  /* NMI flag + CPU version */
#define REG_HVBJOY      (*((volatile uint8_t*)0x4212))  /* H/V blank + auto-joypad status */
#define REG_JOYSER0     (*((volatile uint8_t*)0x4016))  /* Controller port 1 (serial data / GC) */
#define REG_WRIO        (*((volatile uint8_t*)0x4201))  /* Programmable I/O port */

/* =========================================================================
 * Constants
 * ========================================================================= */

/* INIDISP: bit 7 = force blank, bits 0-3 = brightness (0=dark, 15=full) */
#define INIDISP_FORCE_BLANK     0x80
#define INIDISP_BRIGHTNESS_FULL 0x0F

/* BGMODE: Mode 1 with BG3 high priority */
#define BGMODE_1_BG3PRIO        0x09

/* VMAIN: word access, increment by 1 after writing high byte */
#define VMAIN_WORD_INC1         0x80

/* TM layer bits */
#define TM_BG1  0x01
#define TM_BG2  0x02
#define TM_BG3  0x04
#define TM_OBJ  0x10
#define TM_DEFAULT (TM_BG1 | TM_BG3 | TM_OBJ)        /* 0x15 */
#define TM_ALL     (TM_BG1 | TM_BG2 | TM_BG3 | TM_OBJ) /* 0x17 */

/* VRAM tilemap base addresses (each unit = 0x400 words = 0x800 bytes) */
#define VRAM_BG1_TILEMAP   0x7000   /* BG1SC = 0x70 → 0x7000 */
#define VRAM_BG2_TILEMAP   0x7800   /* BG2SC = 0x78 → 0x7800 */
#define VRAM_BG3_TILEMAP   0x7400   /* BG3SC = 0x74 → 0x7400 */
#define VRAM_BG2_OVERLAY   0x7C00   /* BG2SC = 0x7C → 0x7C00 (controller overlay) */
#define VRAM_CHR_BG        0x0000   /* BG12NBA = 0 → chr at 0x0000 */
#define VRAM_CHR_OBJ       0x6000   /* BG34NBA = 0x06 → chr at 0x6000 */

/* Tilemap row/column size */
#define TILEMAP_COLS    32
#define TILEMAP_ROWS    28

/* GC controller button bit masks (in gc_buttons[0]) */
#define GC_BTN_A        0x01
#define GC_BTN_B        0x02
#define GC_BTN_X        0x04
#define GC_BTN_Y        0x08
#define GC_BTN_START    0x10

/* GC controller button bit masks (in gc_buttons[1]) */
#define GC_BTN_DLEFT    0x02
#define GC_BTN_DRIGHT   0x04
#define GC_BTN_DDOWN    0x08
#define GC_BTN_DUP      0x10
#define GC_BTN_Z        0x20
#define GC_BTN_R_DIG    0x40
#define GC_BTN_L_DIG    0x80

/* Combined hold-detect for A+Z = start calibration */
#define GC_COMBO_AZ     0xC0   /* A=0x40, Z=0x80 in combined field used by code */

/* Palette tile attribute: "lit" palette (palette 2 = 0x0400 in tilemap word) */
#define TILE_PAL_LIT    0x0400

/* Calibration center for analog axes: nominal center is 0x80 */
#define AXIS_CENTER     0x80

/* Timeout: frames until auto-reset if no input */
#define TIMEOUT_FRAMES  30

/* SPC700 ready handshake value */
#define SPC_READY       0xBBAA

/* OAM: sprites parked off-screen */
#define OAM_X_HIDDEN    0xE8    /* X=232 hides sprite */
#define OAM_BUF_START   0x00C9  /* WRAM address of OAM shadow buffer */
#define OAM_BUF_SIZE    0x0220  /* 128 sprites × 4 bytes + 32 attribute bytes */

/* =========================================================================
 * WRAM / Direct Page variable layout
 *
 * The 65816 direct page (DP) is set to 0x0000 (bank 0 WRAM).
 * All DP variables occupy 0x0000–0x02FF in WRAM.
 * ========================================================================= */

/* DMA helper scratch registers (reused per call, not persistent state) */
static uint16_t dp_dma_src_addr;       /* 0x00: DMA source address */
static uint8_t  dp_dma_src_bank;       /* 0x02: DMA source bank */
static uint16_t dp_dma_dst_addr;       /* 0x04: VRAM/CGRAM/WRAM destination */
static uint16_t dp_dma_dst_bank;       /* 0x06: WRAM destination bank (for WRAM DMA) */
static uint16_t dp_dma_size;           /* 0x10: DMA byte count */
static uint16_t dp_vram_addr;          /* 0x12: VRAM word address for DMA_VRAM_Write */

/* Sprite/rect helper scratch registers */
static uint16_t dp_rect_width;         /* 0x20: rect width in tiles / sprite table ptr */
static uint16_t dp_rect_height;        /* 0x22: rect height in tiles */
static uint16_t dp_rect_col;           /* 0x24: rect left column */
static uint16_t dp_rect_row;           /* 0x26: rect top row */
static uint16_t dp_palette;            /* 0x28: palette attribute to apply */
static uint16_t dp_sprite_x_offset;    /* 0x20: also used as sprite X offset */
static uint16_t dp_sprite_y_offset;    /* 0x22: also used as sprite Y offset */

/* Misc scratch */
static uint16_t dp_scratch_10;         /* 0x10 (dual use with DMA) */
static uint16_t dp_scratch_12;         /* 0x12 */
static uint16_t dp_loop_counter;       /* 0x36: sprite loop iteration counter */
static uint16_t dp_frame_flag;         /* 0x38: per-frame work flag */

/* GC controller raw receive buffer (8 bytes from GC, stored as DP words) */
static uint8_t gc_raw[8];              /* 0x3E-0x4D: raw bits shifted in from JOYSER0 */
/*
 * gc_raw[0] = 0x3E: GC byte 0 (buttons: A/B/X/Y/Start)
 * gc_raw[1] = 0x40: GC byte 1 (buttons: L/R/Z/D-pad)
 * gc_raw[2] = 0x42: GC byte 2 → main stick X (reassembled to gc_stick_x)
 * gc_raw[3] = 0x44: GC byte 3 → main stick Y (reassembled to gc_stick_y)
 * gc_raw[4] = 0x46: GC byte 4 → C-stick X    (reassembled to gc_cstick_x)
 * gc_raw[5] = 0x48: GC byte 5 → C-stick Y    (reassembled to gc_cstick_y)
 * gc_raw[6] = 0x4A: GC byte 6 → L trigger    (reassembled to gc_trigger_l)
 * gc_raw[7] = 0x4C: GC byte 7 → R trigger    (reassembled to gc_trigger_r)
 */

/* GC controller cooked analog values (after bit reassembly + inversion) */
static uint8_t gc_stick_x;     /* 0x4E: main stick X, 0x00=left, 0xFF=right */
static uint8_t gc_stick_y;     /* 0x50: main stick Y, 0x00=down, 0xFF=up */
static uint8_t gc_cstick_x;    /* 0x52: C-stick X */
static uint8_t gc_cstick_y;    /* 0x54: C-stick Y */
static uint8_t gc_trigger_l;   /* 0x56: L analog trigger (0=released, 0xFF=full) */
static uint8_t gc_trigger_r;   /* 0x58: R analog trigger */

/* Joystick indicator sprite color (set based on D-pad / stick direction) */
static uint16_t joypad_indicator_color;  /* 0x5A: current indicator palette color */
static uint16_t joypad_prev_color;       /* 0x5C: previous indicator color (saved each frame) */

/* Calibrated (relative) analog values: value - center + 0x80 */
static uint8_t gc_rel_stick_x;     /* 0x5E: main stick X relative to calibration center */
static uint8_t gc_rel_stick_y;     /* 0x60: main stick Y relative to calibration center */
static uint8_t gc_rel_cstick_x;    /* 0x62: C-stick X relative */
static uint8_t gc_rel_cstick_y;    /* 0x64: C-stick Y relative */

/* Misc state variables */
static uint8_t  unk_6c;             /* 0x6C: analog display counter / flag */
static uint8_t  unk_6e;             /* 0x6E: misc flag */
static uint8_t  unk_70;             /* 0x70: misc */
static uint16_t timeout_counter;    /* 0x72: no-input timeout frame counter */
static uint8_t  unk_74;             /* 0x74: misc */
static uint16_t init_counter;       /* 0x76: initialization counter (starts at 0x0100) */

/* Calibration centers saved on A+Z hold */
static uint8_t calib_stick_x;      /* 0x78: main stick X center (raw value at calibration) */
static uint8_t calib_stick_y;      /* 0x7A: main stick Y center */
static uint8_t calib_cstick_x;     /* 0x7C: C-stick X center */
static uint8_t calib_cstick_y;     /* 0x7E: C-stick Y center */

/* Misc state (0x80–0x89) */
static uint8_t unk_80[10];

/* Button/axis pass-fail state array (48 bytes, indexed by test sub-state) */
static uint8_t test_state[48];     /* 0x8A–0xB9: per-input test result flags */

/* State machine */
static uint16_t main_state;        /* 0xB9: main test state (0–15) */
static uint16_t sub_state;         /* 0xBB: sub-state within state 2 (0–0x13) */

/* Misc display state */
static uint8_t unk_88;             /* 0x88: misc display flag */

/* Tilemap write pointer used by Write_Digit_To_Tilemap */
static uint16_t tilemap_write_ptr; /* 0xC3: WRAM pointer to current tilemap write position */

/* OAM shadow buffer: 128 OAM entries + 32 size attribute bytes */
/* Located at WRAM 0x00C9–0x02E8 */
struct OAMEntry {
    uint8_t x;       /* X position (low 8 bits) */
    uint8_t y;       /* Y position */
    uint8_t tile;    /* Tile number */
    uint8_t attr;    /* Palette, priority, flip flags */
};
static struct OAMEntry oam_buf[128]; /* at WRAM 0x00C9 */
static uint8_t  oam_size_buf[32];   /* at WRAM 0x02C9: high X bits + size bits */

/* =========================================================================
 * ROM data tables (read-only, addresses in ROM)
 * ========================================================================= */

/*
 * OAM size attribute bit masks (indexed by sprite_index & 3, gives 2-bit field).
 * Used to set/clear the size bit in the 4-sprite oam_size_buf byte.
 * Bit pattern: bit 0 = high X bit, bit 1 = size select
 */
extern const uint16_t oam_size_masks[4];   /* at ROM 0x829A: { 0x0002, 0x0008, 0x0020, 0x0080 } */

/*
 * Screen region tables for 24 button/axis display regions (all ROM 0x9290–0x9397).
 * Each table has 24 entries indexed by region index 0–23.
 */
extern const uint16_t region_col[24];       /* 0x9290: tilemap column for each region */
extern const uint16_t region_row[24];       /* 0x92BC: tilemap row for each region */
extern const uint16_t region_width[24];     /* 0x92E8: width in tiles */
extern const uint16_t region_height[24];    /* 0x9314: height in tiles */
extern const uint16_t region_btn_index[24]; /* 0x9340: index into gc_raw[] for this region's button */
extern const uint16_t region_btn_value[24]; /* 0x936C: expected button value for "pressed" */
extern const uint16_t region_btn_mask[24];  /* 0x9398: bit mask to AND with button byte */

/*
 * Tilemap string data: SNES tilemap word pairs (tile_index, attr_byte).
 * High byte 0x3C or 0x2C gives priority/palette; low byte is tile index.
 */
extern const uint16_t str_up_down_error[];     /* 0x8F71 */
extern const uint16_t str_right_left_error[];  /* 0x8F8B */
extern const uint16_t str_please[];            /* 0x8FAB: "PLEASE" */
extern const uint16_t str_connect[];           /* 0x8FB3: "CONNECT" */
extern const uint16_t str_controller[];        /* 0x8FC3: "CONTROLLER" */
extern const uint16_t str_dsr_error[];         /* 0x8FDD: "DSR error" */
extern const uint16_t str_rxo_error[];         /* 0x8FEB: "RXO error" */
extern const uint16_t str_short_error[];       /* 0x8FFB: "SHORT error" */
extern const uint16_t str_push[];              /* 0x9009: "PUSH" */
extern const uint16_t str_mode2[];             /* 0x902B: "MODE2" */
extern const uint16_t str_checking[];          /* 0x9030: "CHECKING" */
extern const uint16_t str_error[];             /* 0x906B: "ERROR!" */
extern const uint16_t str_led_off[];           /* 0x908B: "LED OFF" */
extern const uint16_t str_push_select[];       /* 0x90AB: "PUSH > SELECT" */
extern const uint16_t str_button_check[];      /* 0x90BB: "BUTTON CHECK" */
extern const uint16_t str_led_red[];           /* 0x90CB: "LED RED" */
extern const uint16_t str_led_green[];         /* 0x90DB: "LED GREEN" */
extern const uint16_t str_push_start[];        /* 0x90EB: "PUSH START" */
extern const uint16_t str_button[];            /* 0x9162: "BUTTON" */

/*
 * SPC700 program ROM: blank/silent SPC program uploaded to APU on init.
 * Block descriptor table at ROM 0x8000 bank 3 (0x038000).
 * Format: { uint16_t dest_addr, uint16_t size, uint8_t data[size] }, terminated by 0x0001
 */
extern const uint8_t spc_program_table[];   /* ROM 0x038000 */

/*
 * Background tilemap data loaded into WRAM on state 0 init.
 * 0x1000 bytes from ROM 0x02E900 → WRAM 0x0000
 */
extern const uint8_t bg_tilemap_data[];     /* ROM 0x02E900 */

/*
 * Tile graphics data:
 *   0x018200–0x01BFFF: 4bpp controller button tiles (VRAM 0x0000, 0x4000 bytes)
 *   0x01C200–0x022FFF: 4bpp controller body tiles  (VRAM 0x4000, 0x1000 bytes)
 *   0x01F200–...     : 2bpp BG3 ui tiles           (VRAM 0x6000, 0x2000 bytes)
 *   0x02EA00–...     : BG2 overlay tilemap          (VRAM 0x7C00–0x7FFF)
 *   0x01EA00–...     : Palette data (0x0200 bytes → CGRAM)
 */

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

void DMA_VRAM_CopyROM(uint16_t src_addr, uint8_t src_bank,
                      uint16_t vram_dst, uint16_t size);
void DMA_VRAM_Write(void);
void DMA_WRAM_ZeroRange(uint16_t wram_dst_addr, uint8_t wram_dst_bank,
                        uint16_t src_addr, uint8_t src_bank, uint16_t size);
void DMA_WRAM_Write(void);
void DMA_CGRAM_Write(void);
void DMA_OAM_Upload(void);
void Init_OAM_Buffer(void);
void Init_PPU_Registers(void);
void WaitVBlank(void);
void SPC700_Upload(uint16_t table_addr, uint8_t table_bank);
void Read_GC_Controller(void);
void Write_Sprites(void);
void Copy_Tilemap_Rect(void);
void Clear_Tilemap_Rect(void);
void Clear_Axis_Vars(void);
void Update_Button_Display(void);
void Set_Tilemap_Palette(void);
void Display_Analog_Values(void);
void Write_Hex_Byte_To_Tilemap(uint8_t value, uint16_t wram_tile_addr);
void Draw_Connect_Screen(void);
void Clear_Connect_Screen(void);
void Timeout_Counter(void);
void Show_Main_Layer(void);
void Show_Controller_Layer(void);

/* State handlers */
void State0_Init(void);
void State1_WaitCalibration(void);
void State2_ButtonSequence(void);
void State3_StickZone(void);
void State4_AxisTest(void);
void State5_StickRange(void);
void State6_CStickZone(void);
void State8_CStickRange(void);
void State9_TriggerTest(void);
void State10_ZTriggerTest(void);
void State11_CStickY(void);
void State12_CStickX(void);
void State13_Complete(void);
void State14_ShowMainScreen(void);
void State15_ShowControllerOverlay(void);

/* =========================================================================
 * DMA helper functions
 * ========================================================================= */

/*
 * DMA_VRAM_Write  (0x8018)
 * DMA transfer from ROM/WRAM to VRAM using DMA channel 0.
 * Uses dp_dma_src_addr / dp_dma_src_bank as source,
 * dp_vram_addr as VRAM word destination, dp_dma_size as byte count.
 * Mode: fixed source address (DMAP0 = 0x09), writes to VMDATAL (0x18).
 */
void DMA_VRAM_Write(void)
{
    REG_DMAP0  = 0x09;               /* DMA mode: fixed, write low byte only */
    REG_BBAD0  = 0x18;               /* B-bus: VMDATAL */
    REG_A1T0   = dp_dma_src_addr;
    REG_A1B0   = dp_dma_src_bank;
    REG_DAS0   = dp_dma_size;
    REG_VMAIN  = VMAIN_WORD_INC1;
    REG_VMADDL = (uint8_t)(dp_vram_addr);
    REG_VMADDH = (uint8_t)(dp_vram_addr >> 8);
    REG_MDMAEN = 0x01;               /* Enable DMA channel 0 */
}

/*
 * DMA_WRAM_Write  (0x8049)
 * DMA transfer from ROM to WRAM via WMDATA port (0x2180).
 * Uses dp_dma_src_addr (0x00) / dp_dma_src_bank (0x02) as source,
 * WMADDL/H set from dp_dma_dst_addr (0x04) / dp_dma_dst_bank (0x06),
 * dp_dma_size (0x10) as byte count.
 * Also zeroes OAM size buffer at 0x02C9 (0x20 bytes) after transfer.
 */
void DMA_WRAM_Write(void)
{
    REG_WMADDL = (uint8_t)(dp_dma_dst_addr);
    REG_WMADDM = (uint8_t)(dp_dma_dst_addr >> 8);
    REG_WMADDH = (uint8_t)dp_dma_dst_bank;

    REG_DMAP0  = 0x08;               /* DMA mode: fixed destination */
    REG_BBAD0  = 0x80;               /* B-bus: WMDATA */
    REG_A1T0   = dp_dma_src_addr;
    REG_A1B0   = dp_dma_src_bank;
    REG_DAS0   = dp_dma_size;
    REG_MDMAEN = 0x01;

    /* Zero OAM size buffer */
    for (int i = 0; i < 0x20; i++)
        oam_size_buf[i] = 0;
}

/*
 * DMA_CGRAM_Write  (0x808C)
 * DMA transfer from ROM to CGRAM (palette RAM).
 * Uses dp_dma_src_addr/bank (0x00/0x02) as source,
 * dp_dma_dst_addr (0x04) as CGRAM start address,
 * dp_dma_size (0x10) as byte count.
 */
void DMA_CGRAM_Write(void)
{
    REG_CGADD  = (uint8_t)dp_dma_dst_addr;  /* Set CGRAM write address */
    REG_DMAP0  = 0x02;               /* DMA mode: word (low/high alternating) */
    REG_BBAD0  = 0x22;               /* B-bus: CGDATA */
    REG_A1T0   = dp_dma_src_addr;
    REG_A1B0   = dp_dma_src_bank;
    REG_DAS0   = dp_dma_size;
    REG_MDMAEN = 0x01;
}

/*
 * DMA_VRAM_Write2  (0x80B6)
 * DMA transfer from ROM to VRAM (alternate parameter layout).
 * dp_dma_src_addr (0x00) / dp_dma_src_bank (0x02): source
 * dp_dma_dst_addr (0x04): VRAM word destination
 * dp_dma_size (0x10): byte count
 * Mode 0x01: two-register (writes VMDATAL and VMDATAH alternately).
 */
void DMA_VRAM_Write2(void)
{
    REG_VMAIN  = VMAIN_WORD_INC1;
    REG_VMADD  = dp_dma_dst_addr;
    REG_DMAP0  = 0x01;               /* DMA mode: two-register (VMDATAL/VMDATAH) */
    REG_BBAD0  = 0x18;               /* B-bus: VMDATAL */
    REG_A1T0   = dp_dma_src_addr;
    REG_A1B0   = dp_dma_src_bank;
    REG_DAS0   = dp_dma_size;
    REG_MDMAEN = 0x01;
}

/*
 * DMA_WRAM_Write2  (0x80E5)
 * DMA from ROM to WRAM (alternate parameter layout matching DMA_VRAM_Write2).
 * dp_dma_dst_addr (0x04): WRAM destination address
 * dp_dma_dst_bank (0x06): WRAM destination bank
 * dp_dma_src_addr (0x00) / dp_dma_src_bank (0x02): source
 * dp_dma_size (0x10): byte count
 */
void DMA_WRAM_Write2(void)
{
    REG_WMADDL = (uint8_t)(dp_dma_dst_addr);
    REG_WMADDM = (uint8_t)(dp_dma_dst_addr >> 8);
    REG_WMADDH = (uint8_t)dp_dma_dst_bank;
    REG_DMAP0  = 0x00;               /* DMA mode: single, increment source */
    REG_BBAD0  = 0x80;               /* B-bus: WMDATA */
    REG_A1T0   = dp_dma_src_addr;
    REG_A1B0   = dp_dma_src_bank;
    REG_DAS0   = dp_dma_size;
    REG_MDMAEN = 0x01;
}

/*
 * Init_OAM_Buffer  (0x8114)
 * Initializes the OAM shadow buffer:
 *   - Fills entries 0x00C9..0x02C9 with 0xE8E8 (X=232, Y=232 → hidden off screen)
 *   - Zeroes entries 0x02C9..0x02E8 (size attribute bytes)
 */
void Init_OAM_Buffer(void)
{
    /* Fill main OAM entries with "hidden" position (X=E8, Y=E8) */
    for (int i = 0; i < 128; i++) {
        oam_buf[i].x    = OAM_X_HIDDEN;
        oam_buf[i].y    = OAM_X_HIDDEN;
        oam_buf[i].tile = 0;
        oam_buf[i].attr = 0;
    }
    /* Zero size attribute bytes */
    for (int i = 0; i < 32; i++)
        oam_size_buf[i] = 0;
}

/*
 * DMA_OAM_Upload  (0x8136)
 * Uploads OAM shadow buffer to PPU OAM via DMA channel 0.
 * Source: WRAM 0x00C9, size: 0x0220 bytes (128 entries + 32 size bytes).
 */
void DMA_OAM_Upload(void)
{
    REG_OAMADDL = 0;
    REG_OAMADDH = 0;
    REG_DMAP0   = 0x02;              /* DMA mode: word (OAM needs both bytes per write) */
    REG_BBAD0   = 0x04;             /* B-bus: OAMDATA */
    REG_A1T0    = OAM_BUF_START;    /* Source: WRAM 0x00C9 */
    REG_A1B0    = 0x00;             /* Source bank: WRAM bank 0 */
    REG_DAS0    = OAM_BUF_SIZE;
    REG_MDMAEN  = 0x01;
}

/* =========================================================================
 * PPU / APU initialization
 * ========================================================================= */

/*
 * Init_PPU_Registers  (0x8172)
 * Zeroes all PPU registers 0x2101–0x2133, writes zero to each scroll
 * register twice (required for 9-bit scroll), zeroes WRIO and multiply
 * registers, then clears all of WRAM (banks 0x00 and 0x7F).
 */
void Init_PPU_Registers(void)
{
    /* Zero PPU registers 0x2101–0x2133 */
    for (int i = 1; i <= 0x33; i++)
        ((volatile uint8_t*)0x2100)[i] = 0;

    /* Zero WRIO (programmable I/O output) */
    REG_WRIO = 0x00;

    /* Zero BG scroll registers (each needs two writes for full 9-bit value) */
    REG_BG1HOFS = 0; REG_BG1HOFS = 0;
    REG_BG1VOFS = 0; REG_BG1VOFS = 0;
    REG_BG2HOFS = 0; REG_BG2HOFS = 0;
    REG_BG2VOFS = 0; REG_BG2VOFS = 0;
    REG_BG3HOFS = 0; REG_BG3HOFS = 0;
    REG_BG3VOFS = 0; REG_BG3VOFS = 0;
    REG_BG4HOFS = 0; REG_BG4HOFS = 0;
    REG_BG4VOFS = 0; REG_BG4VOFS = 0;

    /* Zero DMA/multiply registers 0x4202–0x420C */
    for (int i = 2; i <= 0x0D; i++)
        ((volatile uint8_t*)0x4200)[i] = 0;

    /* Clear direct page (WRAM bank 0, 0x0000–0x1BFF) */
    for (int i = 0; i < 0x1C00; i++)
        ((volatile uint8_t*)0x0000)[i] = 0;

    /* Clear WRAM bank 0x7F (0x7F0000–0x7FFFFF) */
    for (int i = 0; i < 0x10000; i++)
        ((volatile uint8_t*)0x7F0000)[i] = 0;
}

/*
 * WaitVBlank  (0x8163)
 * Spins until the NMI (V-blank) flag is set in RDNMI, then clears it.
 */
void WaitVBlank(void)
{
    while (!(REG_RDNMI & 0x80))
        ;
    /* Read again to acknowledge / clear the NMI flag */
    (void)REG_RDNMI;
}

/*
 * SPC700_Upload  (0x8319)
 * Uploads a program to the SPC700 (APU) using the standard SNES handshake.
 *
 * table_addr / table_bank point to a block descriptor table of the form:
 *   struct { uint16_t dest_addr; uint16_t byte_count; uint8_t data[]; }
 * terminated by a dest_addr of 0x0001.
 *
 * Protocol (Nintendo standard SPC700 transfer):
 *   1. Wait for APU to signal 0xBBAA on ports 0/1
 *   2. Send initial command + destination address + data byte-by-byte
 *   3. For each subsequent block, send a new header with incremented counter
 *   4. Send final "execute" command (dest = 0x0001) to start SPC700
 */
void SPC700_Upload(uint16_t table_addr, uint8_t table_bank)
{
    uint16_t y = 0;      /* byte index into table */
    uint8_t  counter;    /* handshake counter, starts at 0xCC */
    uint8_t  cmd;

    /* Wait for SPC700 ready signal */
    while (REG_APUIO0 != SPC_READY)
        ;

    counter = 0xCC;
    cmd = counter;
    goto send_header;

next_block:
    {
        /* Load next byte from table (command/block marker) */
        uint8_t b = ((const uint8_t*)(table_bank << 16 | table_addr))[y++];
        /* Swap: send high byte first, then build word */
        uint8_t bhi = b;
        uint8_t blo = 0;
        /* Loop: for each byte in block, wait for ACK then send */
        /* (Simplified from inner loop at 0x8335–0x8348) */
        do {
            uint8_t data_byte = ((const uint8_t*)(table_bank << 16 | table_addr))[y++];
            /* Wait for SPC to echo counter */
            while (REG_APUIO0_B != counter)
                ;
            counter++;
            /* Write next byte */
            REG_APUIO0 = (uint16_t)(counter | (data_byte << 8));
        } while (counter != 0);

        /* Wait for SPC to acknowledge final byte */
        while (REG_APUIO0_B != counter)
            ;
    }

    /* Check for more blocks (BVS branch = overflow set = more data) */

send_header:
    {
        /* Read dest address (16-bit) and block size (16-bit) from table */
        uint16_t dest = *(const uint16_t*)((table_bank << 16) | (table_addr + y));
        y += 2;
        uint16_t size = *(const uint16_t*)((table_bank << 16) | (table_addr + y));
        y += 2;

        /* Write block size to APU port 2 */
        REG_APUIO2 = size;

        /* Write dest address high bit to port 1: if dest == 0x0001, signal execute */
        uint8_t ctrl = (dest == 0x0001) ? 0x01 : 0x00;
        REG_APUIO1_B = ctrl;

        /* Send command byte: counter + 0x7F (or +0x80 for last block) */
        cmd = counter + 0x7F + ctrl;
        REG_APUIO0_B = cmd;

        /* Wait for SPC to echo command */
        while (REG_APUIO0_B != cmd)
            ;

        if (dest != 0x0001)
            goto next_block;
        /* else: done — SPC700 will now execute from 0x0000 */
    }
}

/* =========================================================================
 * GC Controller I/O
 * ========================================================================= */

/*
 * Read_GC_Controller  (0x95CB)
 *
 * Reads one full controller report from the GameCube controller connected
 * to controller port 1 (JOYSER0 / 0x4016) using bit-bang serial I/O.
 *
 * GameCube Controller Protocol (200kHz single-wire, active low):
 *   The host sends a 3-byte poll command (0x40 0x03 0x00).
 *   The controller responds with 8 bytes of state data.
 *
 *   Bit timing (approx, at SNES ~21.47 MHz master clock):
 *     "0" bit: data LOW for ~3µs, HIGH for ~1µs
 *     "1" bit: data LOW for ~1µs, HIGH for ~3µs
 *
 *   On SNES, JOYSER0 bit 0 = output data line (to GC controller)
 *             JOYSER0 bit 0 read = input data line (from GC controller)
 *
 * The received 8 bytes are decoded as:
 *   gc_raw[0]: buttons byte 0 (A/B/X/Y/Start/...)
 *   gc_raw[1]: buttons byte 1 (L/R/Z/D-Up/D-Down/D-Right/D-Left)
 *   gc_raw[2–7]: analog axes (reassembled into gc_stick_x/y, gc_cstick_x/y,
 *                              gc_trigger_l, gc_trigger_r)
 *
 * After reading, analog values are inverted (EOR #0xFF) to correct for
 * the active-low data line, then calibrated relative positions are computed.
 */
void Read_GC_Controller(void)
{
    /* Save previous indicator color */
    joypad_prev_color = joypad_indicator_color;

    /*
     * Send GC poll command preamble via bit-bang.
     * Pattern: LOW(6 NOP) → HIGH(5 NOP) → LOW(6 NOP) → HIGH → delay(0x0C)
     * This encodes the start of the 0x40 0x03 0x00 command.
     */
    REG_JOYSER0 = 0x00;
    /* 6 cycle delay (NOPs) */
    REG_JOYSER0 = 0x01;
    /* 5 cycle delay */
    REG_JOYSER0 = 0x00;
    /* 6 cycle delay */
    REG_JOYSER0 = 0x01;
    /* Longer delay: 0x0C loop iterations */
    for (uint8_t d = 0x0C; d != 0; d--)
        ;

    /*
     * Receive 8 bytes from GC controller.
     * Outer loop: X = byte index (0–7), each byte read into gc_raw[X]
     * Inner loop: Y = bit index (0–7)
     */
    for (int byte_idx = 0; byte_idx < 8; byte_idx++) {
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            /*
             * Clock the data line LOW to signal "ready to receive a bit",
             * then sample the controller's response.
             */
            REG_JOYSER0 = 0x00;                    /* clock low = request bit */
            uint8_t bit = REG_JOYSER0 & 0x01;      /* sample data bit */

            /* Shift bit into receive buffer (MSB first via ROR chain) */
            gc_raw[byte_idx] = (gc_raw[byte_idx] >> 1) | (bit << 7);

            /* Write timing signal to WRIO (controller latch line) */
            REG_WRIO = (uint8_t)(gc_raw[byte_idx] >> 1);

            /* Hold-low delay: 8 iterations */
            for (uint8_t d = 0x08; d != 0; d--)
                ;

            /* Clock high = release data line for controller to drive next bit */
            REG_JOYSER0 = 0x01;

            /* Hold-high delay: 10 iterations */
            for (uint8_t d = 0x0A; d != 0; d--)
                ;
        }
        /* Inter-byte gap delay: 0x0C iterations */
        for (uint8_t d = 0x0C; d != 0; d--)
            ;
    }

    /*
     * Reassemble bit-reversed raw bytes into proper analog values.
     * The bits were shifted in LSB-first due to the ROR instruction,
     * so we must reverse each byte's bit order.
     * Then invert (EOR #0xFF) to correct for active-low signal level.
     *
     * Each analog byte is extracted from its gc_raw[] slot using 8× (ROR/ROL) pairs.
     */

    /* Main stick X (gc_raw[2] → gc_stick_x) */
    uint8_t raw = gc_raw[2];
    uint8_t out = 0;
    for (int b = 0; b < 8; b++) {
        out = (out << 1) | (raw & 1);
        raw >>= 1;
    }
    gc_stick_x = out ^ 0xFF;

    /* Main stick Y (gc_raw[3] → gc_stick_y) */
    raw = gc_raw[3]; out = 0;
    for (int b = 0; b < 8; b++) { out = (out << 1) | (raw & 1); raw >>= 1; }
    gc_stick_y = out ^ 0xFF;

    /* C-stick X (gc_raw[4] → gc_cstick_x) */
    raw = gc_raw[4]; out = 0;
    for (int b = 0; b < 8; b++) { out = (out << 1) | (raw & 1); raw >>= 1; }
    gc_cstick_x = out ^ 0xFF;

    /* C-stick Y (gc_raw[5] → gc_cstick_y) */
    raw = gc_raw[5]; out = 0;
    for (int b = 0; b < 8; b++) { out = (out << 1) | (raw & 1); raw >>= 1; }
    gc_cstick_y = out ^ 0xFF;

    /* L trigger (gc_raw[6] → gc_trigger_l) */
    raw = gc_raw[6]; out = 0;
    for (int b = 0; b < 8; b++) { out = (out << 1) | (raw & 1); raw >>= 1; }
    gc_trigger_l = out ^ 0xFF;

    /* R trigger (gc_raw[7] → gc_trigger_r) */
    raw = gc_raw[7]; out = 0;
    for (int b = 0; b < 8; b++) { out = (out << 1) | (raw & 1); raw >>= 1; }
    gc_trigger_r = out ^ 0xFF;

    /*
     * Compute calibrated (relative) stick positions.
     * Formula: relative = (raw - center) + 0x80
     * This maps center → 0x80, full left/up → 0x00, full right/down → 0xFF.
     */
    gc_rel_stick_x  = (uint8_t)(gc_stick_x  - calib_stick_x  + AXIS_CENTER);
    gc_rel_stick_y  = (uint8_t)(gc_stick_y  - calib_stick_y  + AXIS_CENTER);
    gc_rel_cstick_x = (uint8_t)(gc_cstick_x - calib_cstick_x + AXIS_CENTER);
    gc_rel_cstick_y = (uint8_t)(gc_cstick_y - calib_cstick_y + AXIS_CENTER);
}

/* =========================================================================
 * Tilemap / Sprite display helpers
 * ========================================================================= */

/*
 * Clear_Axis_Vars  (0x923A)
 * Zeroes the 48-byte test_state[] array (0x8A–0xB9 in WRAM).
 */
void Clear_Axis_Vars(void)
{
    for (int i = 0; i < 48; i++)
        ((uint8_t*)test_state)[i] = 0;
}

/*
 * Copy_Tilemap_Rect  (0x82A4)
 * Copies tile data from a ROM table into a rectangular region of a WRAM tilemap.
 *
 * dp_dma_src_addr (0x00): pointer to source tile word array in WRAM/ROM
 * dp_dma_dst_addr (0x04): VRAM word address of tilemap base (e.g. VRAM_BG1_TILEMAP)
 * dp_rect_col (0x24): left column
 * dp_rect_row (0x26): top row
 * dp_rect_width (0x20): width in tiles
 * dp_rect_height (0x22): height in tiles
 */
void Copy_Tilemap_Rect(void)
{
    /* Compute VRAM word offset of top-left tile */
    uint16_t base = dp_dma_dst_addr + dp_rect_row * TILEMAP_COLS + dp_rect_col;
    const uint16_t *src = (const uint16_t*)(uintptr_t)dp_dma_src_addr;

    for (uint16_t row = 0; row < dp_rect_height; row++) {
        uint16_t row_addr = base + row * TILEMAP_COLS;
        for (uint16_t col = 0; col < dp_rect_width; col++) {
            /* Write tile word (tile index | palette/priority bits) to WRAM tilemap */
            ((uint16_t*)(uintptr_t)0x0000)[row_addr + col] = *src++;
        }
        /* Advance src pointer: done implicitly */
    }
}

/*
 * Clear_Tilemap_Rect  (0x82E1)
 * Fills a rectangular region of a WRAM tilemap with tile 0x0000 (blank).
 * Same parameters as Copy_Tilemap_Rect.
 */
void Clear_Tilemap_Rect(void)
{
    uint16_t base = dp_dma_dst_addr + dp_rect_row * TILEMAP_COLS + dp_rect_col;

    for (uint16_t row = 0; row < dp_rect_height; row++) {
        uint16_t row_addr = base + row * TILEMAP_COLS;
        for (uint16_t col = 0; col < dp_rect_width; col++)
            ((uint16_t*)(uintptr_t)0x0000)[row_addr + col] = 0x0000;
    }
}

/*
 * Set_Tilemap_Palette  (0x93D0)
 * Changes the palette attribute bits of all tiles in a rectangle
 * without altering the tile index.
 *
 * dp_rect_col (0x24), dp_rect_row (0x26): top-left of region
 * dp_rect_width (0x20), dp_rect_height (0x22): dimensions
 * dp_dma_dst_addr (0x04): VRAM word address of tilemap base
 * dp_palette (0x28): new palette bits (e.g. 0x0400 = palette 2)
 */
void Set_Tilemap_Palette(void)
{
    uint16_t base = dp_dma_dst_addr + dp_rect_row * TILEMAP_COLS + dp_rect_col;

    for (uint16_t row = 0; row < dp_rect_height; row++) {
        uint16_t row_addr = base + row * TILEMAP_COLS;
        for (uint16_t col = 0; col < dp_rect_width; col++) {
            uint16_t *tile = &((uint16_t*)(uintptr_t)0x0000)[row_addr + col];
            /* Clear old palette bits (bits 12:10), apply new palette */
            *tile = (*tile & 0xE3FF) | dp_palette;
        }
    }
}

/*
 * Write_Sprites  (0x81ED)
 * Writes sprite entries into the OAM shadow buffer from a descriptor table.
 *
 * [dp_dma_src_addr] (0x00): pointer to sprite descriptor table
 *   Byte 0 of table: sprite count (number of sprites to write)
 *   Remaining entries: { x_low, y, tile, attr } for each sprite
 *
 * dp_sprite_x_offset (0x20): X offset added to each sprite's X position
 * dp_sprite_y_offset (0x22): Y offset added to each sprite's Y position
 * dp_scratch_10 (0x10): OAM size attribute mask for this sprite group
 * dp_frame_flag (0x38): OAM index / frame counter (low 2 bits select which
 *                        byte in oam_size_buf is affected)
 *
 * The function also handles the OAM "high X bit" and size-select in
 * oam_size_buf[] using the oam_size_masks[] table.
 */
void Write_Sprites(void)
{
    const uint8_t *table = (const uint8_t*)(uintptr_t)dp_dma_src_addr;
    uint8_t count = table[0];
    table++;  /* skip count byte */

    /* Determine OAM size attribute mask from frame_flag[1:0] */
    uint8_t size_group = dp_frame_flag & 0x03;
    uint16_t size_mask    = oam_size_masks[size_group];
    uint16_t size_mask_n  = size_mask ^ 0xFFFF;   /* inverted for clearing */

    /* Determine which oam_size_buf byte and which 4-sprite group */
    uint8_t oam_group = (dp_frame_flag >> 2) & 0x1F;

    for (int i = 0; i < count; i++) {
        uint8_t sx   = table[0];   /* X position low */
        uint8_t sy   = table[1];   /* Y position */
        uint8_t tile = table[2];   /* Tile number */
        uint8_t attr = table[3];   /* Attributes */
        table += 4;

        /* Apply sprite table offsets */
        int16_t final_x = (int16_t)sx + (int16_t)(int8_t)dp_sprite_x_offset;
        int16_t final_y = (int16_t)sy + (int16_t)(int8_t)dp_sprite_y_offset;

        /* Write to OAM buffer slot dp_frame_flag */
        int slot = dp_frame_flag & 0x7F;
        oam_buf[slot].x    = (uint8_t)final_x;
        oam_buf[slot].y    = (uint8_t)final_y;
        oam_buf[slot].tile = tile;
        oam_buf[slot].attr = attr;

        /* Update OAM size byte: set or clear size bit based on table entry */
        if (final_x >= 0) {
            oam_size_buf[oam_group] |=  (uint8_t)size_mask;   /* set bit: large sprite */
        } else {
            oam_size_buf[oam_group] &= (uint8_t)size_mask_n;  /* clear bit: small sprite */
        }

        dp_frame_flag++;
    }
}

/*
 * Update_Button_Display  (0x924A)
 * For each of 24 display regions (buttons and axes), reads the
 * corresponding GC controller byte, applies the region's bit mask,
 * and compares to the expected "pressed" value.
 * Sets the tilemap palette to "lit" (TILE_PAL_LIT) if pressed,
 * or leaves the default test_state palette if not.
 */
void Update_Button_Display(void)
{
    for (int i = 0; i < 24; i++) {
        /* Set up rectangle geometry for this region */
        dp_rect_col    = region_col[i];
        dp_rect_row    = region_row[i];
        dp_rect_width  = region_width[i];
        dp_rect_height = region_height[i];
        dp_dma_dst_addr = VRAM_BG1_TILEMAP;  /* BG1 tilemap in WRAM */

        /* Read button byte for this region */
        uint8_t btn_byte_idx = (uint8_t)region_btn_index[i];
        uint8_t btn_byte     = gc_raw[btn_byte_idx];

        /* Check if button is pressed */
        uint8_t masked = btn_byte & (uint8_t)region_btn_mask[i];
        if (masked == (uint8_t)region_btn_value[i]) {
            dp_palette = TILE_PAL_LIT;       /* Button pressed: bright palette */
        } else {
            /* Not pressed: use accumulated test_state value as dim palette */
            dp_palette = ((uint8_t*)test_state)[i * 2];
        }

        Set_Tilemap_Palette();
    }
}

/*
 * Write_Hex_Byte_To_Tilemap  (0x98FA + caller)
 * Writes a single byte as two hex digit tiles at a given WRAM tilemap address.
 *
 * value: 8-bit value to display (e.g. 0xAB → tiles 'A', 'B')
 * wram_tile_addr: word address in WRAM tilemap to write into
 */
void Write_Hex_Byte_To_Tilemap(uint8_t value, uint16_t wram_tile_addr)
{
    /* Tile index for digits: 0x30+'0'=0x30..0x39, 'A'=0x41..
     * The ROM stores them with attr byte 0x3C (palette + priority) in high byte */
    uint8_t hi_nibble = (value >> 4) & 0x0F;
    uint8_t lo_nibble = value & 0x0F;

    uint8_t hi_tile = hi_nibble < 10 ? (0x30 + hi_nibble) : (0x37 + hi_nibble);
    uint8_t lo_tile = lo_nibble < 10 ? (0x30 + lo_nibble) : (0x37 + lo_nibble);

    uint16_t *tilemap = (uint16_t*)(uintptr_t)0x0000;
    tilemap[wram_tile_addr]     = 0x3C00 | hi_tile;
    tilemap[wram_tile_addr + 1] = 0x3C00 | lo_tile;
}

/*
 * Display_Analog_Values  (0x9408)
 * Writes all six analog axis values as two-digit hex to the BG1 tilemap,
 * and updates joystick indicator sprites.
 *
 * Tilemap positions (WRAM word addresses relative to BG1 base at 0x00C9):
 *   gc_stick_x   → word 0x117B / 0x117D
 *   gc_stick_y   → word 0x1183 / 0x1185
 *   gc_cstick_x  → word 0x118D / 0x118F
 *   gc_cstick_y  → word 0x1195 / 0x1197
 *   gc_trigger_l → word 0x0CBF / 0x0CC1
 *   gc_trigger_r → word 0x0CCF / 0x0CD1
 *
 * Palette tile addresses for color indicators:
 *   gc_stick_x color at 0x0692/0x0694, gc_stick_y at 0x069A/0x069C
 *   gc_cstick_x at 0x06A4/0x06A6, gc_cstick_y at 0x06AC/0x06AE
 *   gc_trigger_l at 0x01D6/0x01D8, gc_trigger_r at 0x01E6/0x01E8
 */
void Display_Analog_Values(void)
{
    /* Display main stick X */
    Write_Hex_Byte_To_Tilemap(gc_stick_x, 0x117B);

    /* Display main stick Y */
    Write_Hex_Byte_To_Tilemap(gc_stick_y, 0x1183);

    /* Display C-stick X */
    Write_Hex_Byte_To_Tilemap(gc_cstick_x, 0x118D);

    /* Display C-stick Y */
    Write_Hex_Byte_To_Tilemap(gc_cstick_y, 0x1195);

    /* Display L trigger */
    Write_Hex_Byte_To_Tilemap(gc_trigger_l, 0x0CBF);

    /* Display R trigger */
    Write_Hex_Byte_To_Tilemap(gc_trigger_r, 0x0CCF);

    /*
     * Update joystick indicator sprites:
     * The calibrated gc_rel_stick_x/y values (0x00–0xFF, center=0x80)
     * are mapped to sprite screen positions using a lookup in the sprite table.
     * dp_sprite_x_offset/y_offset are computed from the relative positions.
     */
    /* Main stick sprite group */
    /* C-stick sprite group */
    /* (Sprite writes call Write_Sprites with appropriate table pointers) */
}

/*
 * Draw_Connect_Screen  (0x9810)
 * Draws the "PLEASE CONNECT CONTROLLER" message in a rect on BG1.
 */
void Draw_Connect_Screen(void)
{
    dp_dma_dst_addr = VRAM_BG1_TILEMAP;
    dp_rect_col    = 8;
    dp_rect_row    = 13;
    dp_rect_width  = 16;
    dp_rect_height = 1;
    dp_dma_src_addr = (uint16_t)(uintptr_t)str_please;
    Copy_Tilemap_Rect();
}

/*
 * Clear_Connect_Screen  (0x983C)
 * Clears the "PLEASE CONNECT CONTROLLER" message area.
 */
void Clear_Connect_Screen(void)
{
    dp_dma_dst_addr = VRAM_BG1_TILEMAP;
    dp_rect_col    = 8;
    dp_rect_row    = 13;
    dp_rect_width  = 16;
    dp_rect_height = 1;
    Clear_Tilemap_Rect();
}

/* =========================================================================
 * Display layer helpers
 * ========================================================================= */

/*
 * Show_Main_Layer  (0x95A7)
 * Enables all background layers (BG1 + BG2 + BG3 + OBJ) on main screen.
 */
void Show_Main_Layer(void)
{
    REG_TM = TM_ALL;
}

/*
 * Show_Controller_Layer  (0x95B7)
 * Switches BG2 tilemap to the controller overlay at VRAM 0x7C00
 * and enables color math (sub-screen blend) for the overlay effect.
 */
void Show_Controller_Layer(void)
{
    REG_WOBJSEL = 0x02;              /* Color math: sub-screen blend enabled */
    REG_BG2SC   = 0x7C;             /* BG2 tilemap at VRAM 0x7C00 */
    REG_TM      = TM_ALL;
}

/* =========================================================================
 * Timeout counter
 * ========================================================================= */

/*
 * Timeout_Counter  (0x8623)
 * Called every frame. If the state is < 2 (pre-test), resets the counter.
 * Otherwise, increments a no-input counter; if A+Z is not held for
 * TIMEOUT_FRAMES frames, resets the entire test (state = 0, sub_state = 0).
 */
void Timeout_Counter(void)
{
    if (main_state < 2) {
        timeout_counter = 0;
        return;
    }

    /* Check if A+Z combo is held (gc_raw[0] bit A and gc_raw[1] bit Z) */
    uint8_t combo = gc_raw[0] & GC_COMBO_AZ;
    if (combo != GC_COMBO_AZ) {
        /* Button not held: increment timeout */
        timeout_counter++;
        if (timeout_counter >= TIMEOUT_FRAMES) {
            /* Timeout reached: reset to initial state */
            main_state  = 0;
            sub_state   = 0;
        }
    } else {
        /* Button held: reset timeout counter */
        timeout_counter = 0;
    }
}

/* =========================================================================
 * State machine handlers
 * ========================================================================= */

/*
 * State0_Init  (0x8580)
 * Initialization state: loads background tilemap, resets all variables,
 * sets up display layers, advances to state 1.
 */
void State0_Init(void)
{
    /* Clear all test variables and axis state */
    Clear_Axis_Vars();
    init_counter          = 0x0100;
    unk_88                = 0;
    unk_6c                = 0;
    unk_6e                = 0;
    joypad_indicator_color = 0x00E0;

    /* DMA: Load BG1 background tilemap from ROM 0x02E900 into WRAM 0x0000 */
    dp_dma_src_addr = 0xE900;
    dp_dma_src_bank = 0x02;
    dp_dma_dst_addr = 0x0000;
    dp_dma_dst_bank = 0x00;
    dp_dma_size     = 0x1000;
    DMA_WRAM_Write2();

    /* Set BG2SC to default tilemap at 0x7800 */
    REG_BG2SC   = 0x78;
    REG_TM      = TM_DEFAULT;       /* BG1 + BG3 + OBJ only */
    REG_WOBJSEL = 0x00;             /* No color math */

    /* Advance to state 1: waiting for controller connection */
    main_state++;
}

/*
 * State1_WaitCalibration  (0x85E9)
 * Displays "PLEASE CONNECT CONTROLLER" and waits for user to hold A+Z
 * for 10 consecutive frames to calibrate stick centers.
 */
void State1_WaitCalibration(void)
{
    Draw_Connect_Screen();

    /* Check for A+Z hold */
    uint8_t combo = gc_raw[0] & GC_COMBO_AZ;
    if (combo == GC_COMBO_AZ) {
        init_counter--;
        if (init_counter == 0) {
            /* 10 frames held: save calibration centers */
            Clear_Connect_Screen();

            calib_stick_x  = gc_stick_x;
            calib_stick_y  = gc_stick_y;
            calib_cstick_x = gc_cstick_x;
            calib_cstick_y = gc_cstick_y;

            main_state++;   /* → State 2: button/axis test sequence */
        }
    } else {
        init_counter = 0x0100;    /* Reset hold counter if released */
    }
}

/*
 * State2_ButtonSequence  (0x8962)
 * Sequential button/input test. Iterates through 0x13 sub-states,
 * each requiring the user to press or move a specific input.
 * On success, marks the input as passed in test_state[] and advances.
 *
 * Sub-states:
 *   0x00: A button
 *   0x01: B button
 *   0x02: X button
 *   0x03: Y button
 *   0x04: Start button
 *   0x05: D-pad Up
 *   0x06: D-pad Down
 *   0x07: D-pad Left
 *   0x08: D-pad Right
 *   0x09: L digital
 *   0x0A: R digital
 *   0x0B: Z button
 *   0x0C: Main stick – move up
 *   0x0D: Main stick – move down
 *   0x0E: Main stick – move left
 *   0x0F: Main stick – move right
 *   0x10: C-stick – move up
 *   0x11: C-stick – move down
 *   0x12: C-stick – move left
 *   0x13: C-stick – move right
 */
void State2_ButtonSequence(void)
{
    /* Each sub-state is checked against its corresponding button/axis region */
    uint16_t ss = sub_state;

    /* Read the button/axis for this sub-state from GC data */
    uint8_t btn_byte = gc_raw[region_btn_index[ss]];
    uint8_t masked   = btn_byte & (uint8_t)region_btn_mask[ss];

    if (masked == (uint8_t)region_btn_value[ss]) {
        /* Input detected: mark as passed */
        ((uint8_t*)test_state)[ss] = 1;

        /* Hold for required frames before advancing */
        unk_88++;
        uint16_t hold_frames = (ss < 0x0C) ? 5 : 7;
        if (unk_88 >= hold_frames) {
            unk_88 = 0;
            sub_state++;
            if (sub_state > 0x13) {
                sub_state = 0;
                main_state++;   /* → State 3: stick zone detection */
            }
        }
    } else {
        unk_88 = 0;
    }
}

/*
 * State3_StickZone  (0x8BE8)
 * Tests that the main analog stick can reach all four cardinal zones.
 * The stick indicator sprite shows direction; all four zones must be
 * reached to advance.
 */
void State3_StickZone(void)
{
    /* Track which zones have been reached */
    uint8_t zone_up    = (gc_rel_stick_y > 0xC0) ? 1 : 0;
    uint8_t zone_down  = (gc_rel_stick_y < 0x40) ? 1 : 0;
    uint8_t zone_left  = (gc_rel_stick_x < 0x40) ? 1 : 0;
    uint8_t zone_right = (gc_rel_stick_x > 0xC0) ? 1 : 0;

    /* Update zone flags in test_state */
    if (zone_up)    test_state[20] = 1;
    if (zone_down)  test_state[21] = 1;
    if (zone_left)  test_state[22] = 1;
    if (zone_right) test_state[23] = 1;

    /* Set indicator color based on current stick direction */
    if (gc_rel_stick_y > 0xC0)      joypad_indicator_color = 0x0020;  /* Up */
    else if (gc_rel_stick_y < 0x40) joypad_indicator_color = 0x00A0;  /* Down */
    else if (gc_rel_stick_x < 0x40) joypad_indicator_color = 0x0060;  /* Left */
    else if (gc_rel_stick_x > 0xC0) joypad_indicator_color = 0x00E0;  /* Right */

    /* All four zones reached? */
    if (test_state[20] && test_state[21] && test_state[22] && test_state[23])
        main_state++;   /* → State 4 */
}

/*
 * State4_AxisTest  (0x8901)
 * Tests analog axis range (generic handler, used for multiple states).
 * Checks that the axis reaches near-minimum and near-maximum extents.
 */
void State4_AxisTest(void)
{
    /* Check L trigger analog range */
    if (gc_trigger_l > 0xC0) test_state[24] = 1;
    if (gc_trigger_l < 0x40) test_state[25] = 1;
    if (gc_trigger_r > 0xC0) test_state[26] = 1;
    if (gc_trigger_r < 0x40) test_state[27] = 1;

    if (test_state[24] && test_state[25] && test_state[26] && test_state[27])
        main_state++;
}

/*
 * State5_StickRange  (0x8B0A)
 * Checks that the main stick reaches full range on both axes.
 */
void State5_StickRange(void)
{
    if (gc_rel_stick_x < 0x10) test_state[28] = 1;  /* Full left */
    if (gc_rel_stick_x > 0xF0) test_state[29] = 1;  /* Full right */
    if (gc_rel_stick_y < 0x10) test_state[30] = 1;  /* Full down */
    if (gc_rel_stick_y > 0xF0) test_state[31] = 1;  /* Full up */

    if (test_state[28] && test_state[29] && test_state[30] && test_state[31])
        main_state++;
}

/*
 * State6_CStickZone  (0x8DA1)
 * Tests that the C-stick can reach all four cardinal zones.
 */
void State6_CStickZone(void)
{
    if (gc_rel_cstick_y > 0xC0) test_state[32] = 1;
    if (gc_rel_cstick_y < 0x40) test_state[33] = 1;
    if (gc_rel_cstick_x < 0x40) test_state[34] = 1;
    if (gc_rel_cstick_x > 0xC0) test_state[35] = 1;

    if (gc_rel_cstick_x > 0xC0)      joypad_indicator_color = 0x00E0;
    else if (gc_rel_cstick_x < 0x40) joypad_indicator_color = 0x0060;
    else if (gc_rel_cstick_y > 0xC0) joypad_indicator_color = 0x0020;
    else if (gc_rel_cstick_y < 0x40) joypad_indicator_color = 0x00A0;

    if (test_state[32] && test_state[33] && test_state[34] && test_state[35])
        main_state++;
}

/*
 * State8_CStickRange  (0x8CC3)
 * Checks that the C-stick reaches full range on both axes.
 */
void State8_CStickRange(void)
{
    if (gc_rel_cstick_x < 0x10) test_state[36] = 1;
    if (gc_rel_cstick_x > 0xF0) test_state[37] = 1;
    if (gc_rel_cstick_y < 0x10) test_state[38] = 1;
    if (gc_rel_cstick_y > 0xF0) test_state[39] = 1;

    if (test_state[36] && test_state[37] && test_state[38] && test_state[39])
        main_state++;
}

/*
 * State9_TriggerTest  (0x871F)
 * Tests the L and R analog triggers: user must squeeze each trigger fully.
 * Also checks L/R digital buttons.
 */
void State9_TriggerTest(void)
{
    if (gc_trigger_l > 0xE0) test_state[40] = 1;  /* L trigger full squeeze */
    if (gc_trigger_r > 0xE0) test_state[41] = 1;  /* R trigger full squeeze */

    if (test_state[40] && test_state[41])
        main_state++;
}

/*
 * State10_ZTriggerTest  (0x8796)
 * Tests the Z button (digital only).
 */
void State10_ZTriggerTest(void)
{
    if (gc_raw[1] & GC_BTN_Z) {
        test_state[42] = 1;
        unk_88++;
        if (unk_88 >= 5) {
            unk_88 = 0;
            main_state++;
        }
    } else {
        unk_88 = 0;
    }
}

/*
 * State11_CStickY  (0x8810)
 * Tests C-stick vertical axis extremes.
 */
void State11_CStickY(void)
{
    if (gc_rel_cstick_y > 0xE0) test_state[43] = 1;
    if (gc_rel_cstick_y < 0x20) test_state[44] = 1;

    if (test_state[43] && test_state[44])
        main_state++;
}

/*
 * State12_CStickX  (0x8887)
 * Tests C-stick horizontal axis extremes.
 */
void State12_CStickX(void)
{
    if (gc_rel_cstick_x > 0xE0) test_state[45] = 1;
    if (gc_rel_cstick_x < 0x20) test_state[46] = 1;

    if (test_state[45] && test_state[46])
        main_state++;
}

/*
 * State13_Complete  (0x86AF)
 * Final state: all tests passed. Shows "COMPLETE" or pass screen.
 * Switches to controller overlay view.
 */
void State13_Complete(void)
{
    Show_Controller_Layer();
    /* Display pass indicator; no further state advance — test complete */
}

/*
 * State14_ShowMainScreen  (0x95A7 wrapper)
 * Enables all display layers (used as a display mode state).
 */
void State14_ShowMainScreen(void)
{
    Show_Main_Layer();
}

/*
 * State15_ShowControllerOverlay  (0x95B7 wrapper)
 * Activates the transparent GC controller image overlay on BG2.
 */
void State15_ShowControllerOverlay(void)
{
    Show_Controller_Layer();
}

/* =========================================================================
 * Main initialization
 * ========================================================================= */

/*
 * Main_Init  (0x837C)
 * Called from the reset vector (0x8000). Sets up the full SNES environment:
 * PPU registers, tile graphics, tilemaps, palette, SPC700, OAM,
 * then falls into the main loop.
 *
 * Memory layout after initialization:
 *   VRAM 0x0000–0x3FFF: BG1/BG2 4bpp tile graphics (controller buttons)
 *   VRAM 0x4000–0x5FFF: BG1/BG2 4bpp tile graphics (controller body)
 *   VRAM 0x6000–0x7FFF: BG3 2bpp UI tile graphics
 *   VRAM 0x7000–0x77FF: BG1 tilemap (32×28, main display)
 *   VRAM 0x7400–0x7BFF: BG3 tilemap (32×28, priority UI text)
 *   VRAM 0x7800–0x7FFF: BG2 tilemap (32×28, default background)
 *   VRAM 0x7C00–0x7FFF: BG2 overlay tilemap (GC controller outline)
 *   CGRAM: 256 palette entries loaded from ROM
 */
void Main_Init(void)
{
    Init_PPU_Registers();

    /* Upload SPC700 (APU) program — minimal silent program */
    SPC700_Upload(0x8000, 0x03);

    /* Disable display and interrupts during setup */
    REG_JOYSER0  = 0x00;            /* Clear controller port */
    REG_INIDISP  = INIDISP_FORCE_BLANK;
    REG_NMITIMEN = 0x00;            /* Disable NMI */

    /* Configure PPU for Mode 1 (BG1/BG2 = 4bpp, BG3 = 2bpp) */
    REG_VMAIN  = VMAIN_WORD_INC1;
    REG_BGMODE = BGMODE_1_BG3PRIO; /* Mode 1, BG3 high priority */
    REG_OBSEL  = 0x02;              /* Sprites: 8×8 and 16×16, chr base 0 */
    REG_BG12NBA = 0x00;             /* BG1/2 chr at VRAM 0x0000 */
    REG_BG34NBA = 0x06;             /* BG3/4 chr at VRAM 0x6000 */
    REG_BG1SC  = 0x70;              /* BG1 tilemap at VRAM 0x7000 */
    REG_BG2SC  = 0x78;              /* BG2 tilemap at VRAM 0x7800 */
    REG_BG3SC  = 0x74;              /* BG3 tilemap at VRAM 0x7400 */
    REG_TM     = TM_DEFAULT;        /* Main screen: BG1 + BG3 + OBJ */

    /*
     * DMA tile graphics from ROM to VRAM:
     *   ROM 0x018200 → VRAM 0x0000, 0x4000 bytes (BG1/2 4bpp button tiles)
     *   ROM 0x01C200 → VRAM 0x4000, 0x1000 bytes (BG1/2 4bpp body tiles)
     *   ROM 0x01C000 → VRAM 0x6000, 0x2000 bytes (BG3 2bpp UI tiles)
     */
    dp_dma_src_addr = 0x8200; dp_dma_src_bank = 0x01;
    dp_dma_dst_addr = 0x0000; dp_dma_size     = 0x4000;
    DMA_VRAM_Write2();

    dp_dma_src_addr = 0x8800; dp_dma_src_bank = 0x02;
    dp_dma_dst_addr = 0x4000; dp_dma_size     = 0x1000;
    DMA_VRAM_Write2();

    dp_dma_src_addr = 0xC200; dp_dma_src_bank = 0x01;
    dp_dma_dst_addr = 0x6000; dp_dma_size     = 0x2000;
    DMA_VRAM_Write2();

    /* DMA: BG2 tilemap (background) from ROM to VRAM 0x7800 */
    dp_dma_src_addr = 0xF200; dp_dma_src_bank = 0x01;
    dp_dma_dst_addr = VRAM_BG2_TILEMAP; dp_dma_size = 0x0800;
    DMA_VRAM_Write2();

    /* DMA: BG2 overlay tilemap from ROM to VRAM 0x7C00 */
    dp_dma_src_addr = 0xEA00; dp_dma_src_bank = 0x01;
    dp_dma_dst_addr = VRAM_BG2_OVERLAY; dp_dma_size = 0x0800;
    DMA_VRAM_Write2();

    /* DMA: BG3 tilemap from ROM to VRAM 0x7400 */
    dp_dma_src_addr = 0xE200; dp_dma_src_bank = 0x01;
    dp_dma_dst_addr = VRAM_BG3_TILEMAP; dp_dma_size = 0x0800;
    DMA_VRAM_Write2();

    /* DMA: OAM sprite attribute table from ROM to WRAM 0x02E9 */
    dp_dma_src_addr = 0xE200; dp_dma_src_bank = 0x01;
    dp_dma_dst_addr = 0x02E9; dp_dma_dst_bank = 0x00;
    dp_dma_size     = 0x0800;
    DMA_WRAM_Write2();

    /* DMA: OAM sprite attribute data (second region) to WRAM 0x0AE9 */
    dp_dma_src_addr = 0x8000; dp_dma_src_bank = 0x02;
    dp_dma_dst_addr = 0x0AE9; dp_dma_dst_bank = 0x00;
    dp_dma_size     = 0x0800;
    DMA_WRAM_Write2();

    /* DMA: Palette from ROM 0x018000 to CGRAM (0x0200 bytes = 256 colors) */
    dp_dma_src_addr = 0x8000; dp_dma_src_bank = 0x01;
    dp_dma_dst_addr = 0x0000;   /* CGRAM address 0 */
    dp_dma_size     = 0x0200;
    DMA_CGRAM_Write();

    /* Turn display on at full brightness */
    REG_INIDISP = INIDISP_BRIGHTNESS_FULL;

    /* Reset state machine */
    main_state  = 0;
    sub_state   = 0;
    unk_88      = 0;
    unk_6c      = 0;
    unk_6e      = 0;

    Clear_Axis_Vars();

    /* Fall through to main loop */
}

/* =========================================================================
 * Main game loop
 * ========================================================================= */

/* State handler function pointer table (dispatch at ROM 0x84FA) */
typedef void (*StateHandler)(void);
static const StateHandler state_handlers[] = {
    State0_Init,                /* State  0: Initial setup */
    State1_WaitCalibration,     /* State  1: PLEASE CONNECT CONTROLLER */
    State2_ButtonSequence,      /* State  2: Sequential button/axis test */
    State3_StickZone,           /* State  3: Main stick zone detection */
    State4_AxisTest,            /* State  4: Analog axis range check */
    State5_StickRange,          /* State  5: Main stick full range */
    State6_CStickZone,          /* State  6: C-stick zone detection */
    State4_AxisTest,            /* State  7: (reuses axis test handler) */
    State8_CStickRange,         /* State  8: C-stick full range */
    State9_TriggerTest,         /* State  9: L/R trigger squeeze */
    State10_ZTriggerTest,       /* State 10: Z button */
    State11_CStickY,            /* State 11: C-stick Y extreme */
    State12_CStickX,            /* State 12: C-stick X extreme */
    State13_Complete,           /* State 13: All tests passed */
    State14_ShowMainScreen,     /* State 14: Show main display */
    State15_ShowControllerOverlay, /* State 15: Controller overlay view */
};

/*
 * Main_Loop  (0x84B4)
 * Runs every frame (60Hz NTSC / 50Hz PAL):
 *   1. Wait for V-blank
 *   2. DMA tilemap from WRAM to VRAM
 *   3. Upload OAM shadow buffer to PPU
 *   4. Read GC controller
 *   5. Dispatch to current state handler
 *   6. Update timeout counter
 *   7. Refresh button/axis display colors
 *   8. Write analog values as hex digits
 *   9. Loop
 */
void Main_Loop(void)
{
    for (;;) {
        WaitVBlank();

        /* DMA: Copy WRAM tilemap shadow to VRAM BG1 (0x1000 bytes = 32×28 × 2) */
        dp_dma_src_addr = 0x02E9;
        dp_dma_src_bank = 0x00;
        dp_dma_dst_addr = VRAM_BG1_TILEMAP;
        dp_dma_size     = 0x1000;
        DMA_VRAM_Write2();

        /* Upload OAM shadow to PPU */
        DMA_OAM_Upload();

        /* Refresh OAM buffer from WRAM */
        dp_dma_src_addr = 0x00C9;
        dp_dma_src_bank = 0x00;
        dp_dma_dst_addr = 0x00C9;
        dp_dma_dst_bank = 0x00;
        dp_dma_size     = 0x0200;
        DMA_WRAM_Write();

        /* Clear per-frame flag */
        dp_frame_flag = 0;

        /* Read GameCube controller state via bit-bang */
        Read_GC_Controller();

        /* Dispatch to current state handler */
        state_handlers[main_state]();

        /* Check for timeout (no A+Z input → reset) */
        Timeout_Counter();

        /* Refresh button highlight colors in tilemap */
        Update_Button_Display();

        /* Write analog values as hex digits to tilemap */
        Display_Analog_Values();
    }
}

/* =========================================================================
 * Reset entry point
 * ========================================================================= */

/*
 * Reset  (0x8000)
 * SNES hardware reset / power-on entry point.
 * Switches CPU to native mode, sets up stack and direct page,
 * then calls Main_Init which never returns.
 *
 * Equivalent 65816 startup (not expressible in portable C):
 *   clc / xce            ; switch to native (16-bit) mode
 *   sei                  ; disable IRQ
 *   rep #0x30            ; 16-bit A, X, Y
 *   ldx #0x1FFF / txs    ; stack pointer = 0x1FFF
 *   sep #0x30            ; 8-bit A, X, Y
 *   lda #0 / pha / plb   ; data bank register = 0
 *   pha / pha / pld      ; direct page register = 0
 *   jmp Main_Init
 */
void Reset(void)
{
    /* CPU initialized by hardware; stack and DP configured in assembly prologue */
    Main_Init();
    Main_Loop();
    /* never reached */
}
