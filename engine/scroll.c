/* engine/scroll.c - Camera scrolling system
 *
 * Implements NES ScrollHandler (scroll.asm:2-81).
 *
 * Key NES behaviors:
 * - Camera ONLY scrolls RIGHT (never left)
 * - Scroll triggers when player is >80px from screen left edge
 * - Scroll amount = Player_X_Scroll (actual per-frame pixel movement, 1-2 px)
 * - Between 80-112px: scroll amount decremented by 1 (if > 1)
 * - Past 112px: full scroll speed
 * - Player clamped at left/right screen edges (KeepOnscr)
 * - Speed zeroed only when pressing INTO the edge (not away from it)
 */

#include "constants/defs.h"
#include "constants/globals.h"
#include "level/level.h"
#include "opermode.h"
#include "nmi.h"
#include "spr-object.h"
#include "assets.h"

#define SCROLL_THRESHOLD   0x50  /* 80px — scroll.asm:10 */
#define SCROLL_FULL_SPEED  0x70  /* 112px — scroll.asm:23 */

static uint8_t X_SubtracterData[2];
static uint8_t OffscrJoypadBitsData[2];

int Scroll_LoadRom(void) {
    if (Assets_Copy("tables/x_subtracter.bin", X_SubtracterData,
                    sizeof(X_SubtracterData)) ||
        Assets_Copy("tables/offscr_joypad_bits.bin", OffscrJoypadBitsData,
                    sizeof(OffscrJoypadBitsData)))
        return -1;
    return 0;
}

/* Track previous player world X to compute per-frame movement */
static uint16_t s_prev_player_wx = 0;
/* Track last rendered metatile column for scroll-triggered rendering */
static uint16_t s_last_rendered_mt = 24; /* initial parse renders 24 cols */
/* Pending scroll-render columns: entries are metatile column ids; each
 * metatile column drains as TWO tile columns (left then right), one per
 * NMI flush, mirroring the NES per-task buffer fill. */
/* The ROM keeps CurrentPageLoc as a full byte and CurrentColumnPos as a
 * separate nibble.  A byte-only absolute column cursor aliases page 16 back
 * to page 0, so retain the combined page/column coordinate until dispatch. */
static uint16_t s_PendingColIds[4];
static uint8_t s_PendingCols = 0;
static uint8_t s_PendingSub = 0; /* 0 = left tile col, 1 = right tile col */
static uint8_t s_TaskPhase = 0;
static uint8_t s_ParserWroteThisTick = 0;
static uint8_t s_ParserHoldTick = 0;

/* ScrollScreen only advances the byte accumulator; the C parser bridge also
 * needs the two metatile-column IDs that the next AreaParserTaskHandler round
 * will consume.  Keep this producer shared by normal ScrollHandler and the
 * victory-walk caller so both enter the same parser-owned queue. */
static void queue_pending_columns(void) {
    if (g_ScrollThirtyTwo >= 0x20 && s_PendingCols == 0) {
        uint8_t n = 2;
        s_TaskPhase = 0;
        while (n--)
            s_PendingColIds[s_PendingCols++] = s_last_rendered_mt++;
    }
}

int Scroll_AreaParserWrote(void) {
    return s_ParserWroteThisTick != 0;
}

int Scroll_AreaParserDue(void) {
    if (g_AreaParserTaskNum != 0) return 1;
    if (g_ScrollThirtyTwo < 0x20) return 0;
    /* UpdScrollVar subtracts one threshold before entering the task table. */
    g_ScrollThirtyTwo = (uint8_t)(g_ScrollThirtyTwo - 0x20);
    g_VRAM_Buffer2_Offset = 0;
    return s_PendingCols != 0;
}

/* UpdScrollVar (engine/game-mode/core.asm:65-80) is shared by the normal
 * GameEngine tail and PlayerVictoryWalk.  The VRAM controller gate must be
 * observed before advancing one AreaParserTaskHandler subtask. */
void Scroll_RunParserTask(void) {
    extern void AreaParserTaskHandler(void);

    if (g_VRAM_Buffer_AddrCtrl != 0x06 && Scroll_AreaParserDue())
        AreaParserTaskHandler();
}

void Scroll_AreaParserSelectColumn(uint8_t index) {
    uint16_t mt;
    if (index >= s_PendingCols) return;
    mt = s_PendingColIds[index];
    /* AreaParserTaskHandler feeds CurrentPageLoc ($0725) and
     * CurrentColumnPos ($0726) separately.  Keep the page byte intact when
     * selecting a pending absolute metatile coordinate; narrowing here
     * would alias page $10 to page $00 before the ASM-owned render chain. */
    g_CurrentPageLoc = (uint8_t)(mt / 16);
    g_CurrentColumnPos = (uint8_t)(mt & 0x0f);
}

void Scroll_AreaParserRender(uint8_t right_half) {
    uint8_t page = g_CurrentPageLoc;
    uint8_t col = (uint8_t)(g_CurrentColumnPos & 0x0f);
    uint16_t saved_offset = s_NTBaseOffset;

    Level_BeginDeferred();
    s_NTBaseOffset = (page & 1) ? 0x0400 : 0x0000;
    if (right_half)
        Level_RenderColumnRightHalf(page, col);
    else
        Level_RenderColumnLeftHalf(page, col);
    /* RenderAreaGraphics ends through SetVRAMCtrl (main.asm:627/679),
     * selecting Buffer2 only for the two render branches.  Core and
     * IncrementColumnPos leave VRAM_Buffer_AddrCtrl unchanged. */
    g_VRAM_Buffer_AddrCtrl = 0x06;
    s_NTBaseOffset = saved_offset;
}

void Scroll_AreaParserComplete(void) {
    uint8_t i;
    if (s_PendingCols <= 2) {
        s_PendingCols = 0;
    } else {
        for (i = 0; i + 2 < s_PendingCols; i++)
            s_PendingColIds[i] = s_PendingColIds[i + 2];
        s_PendingCols = (uint8_t)(s_PendingCols - 2);
    }
    s_PendingSub = 0;
}

void Scroll_DrainOne(void) {
    s_ParserWroteThisTick = 0;
    if (s_PendingCols == 0) {
        /* The final AreaParserTaskHandler round leaves Buffer2 selected
         * through its following NMI (main.asm:675-679). */
        if (s_ParserHoldTick) {
            s_ParserWroteThisTick = 1;
            s_ParserHoldTick = 0;
        }
        return;
    }
    /* AreaParserTaskHandler (main.asm:1739-1764): after the fire (which
     * runs AreaParserCore itself), the subtask order per frame is
     * R(left half), R(right half), IncCol, Core, R(L), R(R), IncCol.
     * Renders therefore land at fire+1, +2, +5, +6. */
    {
        s_TaskPhase++;
        uint8_t step = s_TaskPhase;
        if (step != 1 && step != 2 && step != 5 && step != 6) {
            if (step >= 7) { s_TaskPhase = 0; s_PendingCols = 0; }
            return;
        }
        uint16_t mt = s_PendingColIds[0];
        uint8_t mt_page = (uint8_t)(mt / 16);
        uint8_t mt_col = (uint8_t)(mt & 0x0f);
        uint16_t saved_offset = s_NTBaseOffset;
        s_NTBaseOffset = (mt_page & 1) ? 0x0400 : 0x0000;
        if (s_PendingSub == 0) {
            /* left half: the preceding Core composed the full metatile */
            Level_BeginDeferred();
            Level_RenderColumnLeftHalf(mt_page, mt_col);
            s_ParserWroteThisTick = 1;
            s_PendingSub = 1;
        } else {
            /* right tile column: consume the same Core-owned composition */
            Level_BeginDeferred();
            Level_RenderColumnRightHalf(mt_page, mt_col);
            s_ParserWroteThisTick = 1;
            s_PendingSub = 0;
            /* shift queue */
            s_PendingCols--;
            s_PendingColIds[0] = s_PendingColIds[1];
            s_PendingColIds[1] = s_PendingColIds[2];
            s_PendingColIds[2] = s_PendingColIds[3];
            if (s_PendingCols == 0) {
                s_TaskPhase = 0; /* column set complete */
                s_ParserHoldTick = 1;
            }
        }
        s_NTBaseOffset = saved_offset;
    }
}

/* ChkPOffscr / KeepOnscr (scroll.asm:54-77). */
static void keep_player_on_screen(void) {
    SprObjectView player = {
        &g_Player_PageLoc, &g_Player_X_Position,
        &g_Player_Y_HighPos, &g_Player_Y_Position,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    SprScreenEdges edges;
    uint8_t bits;
    uint8_t expected;

    SprObject_SetScreenEdges(&edges, g_ScreenLeft_PageLoc,
                             g_ScreenLeft_X_Pos);
    bits = SprObject_GetRawXOffscreenBits(&player, &edges);
    if (bits & 0x80) {
        g_Player_X_Position = edges.left_x;
        g_Player_PageLoc = edges.left_page;
        expected = OffscrJoypadBitsData[0];
    } else if (bits & 0x20) {
        g_Player_PageLoc = edges.right_page;
        g_Player_X_Position = edges.right_x;
        pos_sub_u8(&g_Player_PageLoc, &g_Player_X_Position, X_SubtracterData[1]);
        expected = OffscrJoypadBitsData[1];
    } else {
        return;
    }
    if ((g_SavedJoypadBits & (BTN_LEFT | BTN_RIGHT)) != expected)
        g_Player_X_Speed = 0;
}

static void apply_camera_scroll(void) {
    uint16_t screen_wx = ((uint16_t)g_ScreenLeft_PageLoc << 8) | g_ScreenLeft_X_Pos;
    uint16_t new_left = screen_wx + g_ScrollAmount;
    g_ScreenLeft_X_Pos = (uint8_t)(new_left & 0xFF);
    g_ScreenLeft_PageLoc = (uint8_t)((new_left >> 8) & 0xFF);
    {
        uint16_t right = (uint16_t)g_ScreenLeft_X_Pos + 0xff;
        g_ScreenRight_X_Pos = (uint8_t)right;
        g_ScreenRight_PageLoc = (uint8_t)(g_ScreenLeft_PageLoc +
                                          (right >> 8));
    }
    g_HorizontalScroll = g_ScreenLeft_X_Pos;
    g_ScrollThirtyTwo = (uint8_t)(g_ScrollThirtyTwo + g_ScrollAmount);
    g_MirrorPPUCtrl1 = (uint8_t)((g_MirrorPPUCtrl1 & 0xfe) |
                                  (g_ScreenLeft_PageLoc & 1));
    g_ScrollIntervalTimer = 8;
    queue_pending_columns();
}

void Scroll_Update(void) {
    /* --- ScrollHandler entry (scroll.asm:2-6) ---
     * NES adds the horizontal displacement supplied by PositionPlayerOnHPlat
     * to Player_X_Scroll before deciding how far the camera advances.  The
     * platform routine owns the source byte; this is the single consumer.
     */
    int camera_advances = 1;

    g_Player_X_Scroll = (uint8_t)(g_Player_X_Scroll + g_Platform_X_Scroll);

    if (g_ScrollLock) {
        g_ScrollAmount = 0;
        camera_advances = 0;
    } else if (g_Player_Pos_ForScroll < SCROLL_THRESHOLD) {
        g_ScrollAmount = 0;
        camera_advances = 0;
    } else if (g_SideCollisionTimer != 0) {
        g_ScrollAmount = 0;
        camera_advances = 0;
    } else {
        int8_t y = (int8_t)(g_Player_X_Scroll - 1);
        if (y < 0) {
            g_ScrollAmount = 0;
            camera_advances = 0;
        } else {
            y++;
            if (y >= 2) y--;
            if (g_Player_Pos_ForScroll >= SCROLL_FULL_SPEED)
                y = (int8_t)g_Player_X_Scroll;
            g_ScrollAmount = (uint8_t)y;
        }
    }

    if (camera_advances)
        apply_camera_scroll();

    keep_player_on_screen();
    g_Platform_X_Scroll = 0;
}

void Scroll_Reset(void) {
    uint8_t initial_page = g_ScreenLeft_PageLoc;

    /* InitializeArea writes ScreenLeft_PageLoc from EntrancePage or
     * HalfwayPage before AreaParser_Reset clears the scroll-owned state.
     * The 6502 InitializeMemory pass does not subsequently erase that page;
     * GetScreenPosition only derives the right edge from the preserved left
     * boundary (scroll.asm:89-96). */
    g_ScreenLeft_X_Pos = 0;
    g_ScreenLeft_PageLoc = initial_page;
    g_ScreenRight_X_Pos = 0xff;
    g_ScreenRight_PageLoc = initial_page;
    g_HorizontalScroll = 0;
    g_ScrollAmount = 0;
    g_ScrollThirtyTwo = 0;
    g_ScrollLock = 0;
    g_Platform_X_Scroll = 0;
    s_prev_player_wx = 0;
    /* AreaParserTaskControl renders 24 metatile columns beginning at the
     * InitializeArea entrance page.  ScrollScreen's pending-column cursor is
     * the absolute parser position (CurrentPageLoc:CurrentColumnPos), not a
     * screen-relative column count; seed it with that same entrance-page
     * origin before the first scroll task (main.asm:1434-1442,
     * 1739-1788). */
    s_last_rendered_mt = (uint16_t)initial_page * 16u + 24u;
    g_ScrollThirtyTwo = 0;
    s_PendingCols = 0;
    s_PendingSub = 0;
    s_TaskPhase = 0;
    s_ParserWroteThisTick = 0;
    s_ParserHoldTick = 0;
}

/* ExecGameLoopback (main.asm:4802-4830) relocates the camera four pages
 * backward without running ScrollHandler.  Keep the SDL parser's private
 * cursors at the same world position so its next due task observes the same
 * object/page state as the visible RAM aliases. */
void Scroll_ApplyLoopback(void) {
    uint16_t player_world = (uint16_t)(((uint16_t)g_Player_PageLoc << 8) |
                                       g_Player_X_Position);

    s_prev_player_wx = player_world;
    if (s_last_rendered_mt >= 64)
        s_last_rendered_mt = (uint16_t)(s_last_rendered_mt - 64);
    else
        s_last_rendered_mt = 0;
    s_PendingCols = 0;
    s_PendingSub = 0;
    s_TaskPhase = 0;
    s_ParserWroteThisTick = 0;
    s_ParserHoldTick = 0;
    /* ExecGameLoopback (main.asm:4802-4830) does not touch the scroll
     * accumulator, per-frame amount, or HorizontalScroll.  ScrollHandler
     * already ran earlier in the same GameEngine pass; preserve those
     * physical RAM bytes for the next NMI snapshot. */
}

/* ScrollScreen (game-mode/scroll.asm:27-49), used by PlayerVictoryWalk.
 * This is a camera-state operation, not a framebuffer/OAM substitute. */
void Scroll_Advance(uint8_t amount) {
    uint8_t old_x = g_ScreenLeft_X_Pos;

    g_ScrollAmount = amount;
    g_ScreenLeft_X_Pos = (uint8_t)(old_x + amount);
    if (g_ScreenLeft_X_Pos < old_x) g_ScreenLeft_PageLoc++;
    {
        uint16_t right = (uint16_t)g_ScreenLeft_X_Pos + 0xff;
        g_ScreenRight_X_Pos = (uint8_t)right;
        g_ScreenRight_PageLoc = (uint8_t)(g_ScreenLeft_PageLoc +
                                          (right >> 8));
    }
    g_HorizontalScroll = g_ScreenLeft_X_Pos;
    g_ScrollThirtyTwo = (uint8_t)(g_ScrollThirtyTwo + amount);
    queue_pending_columns();
    g_MirrorPPUCtrl1 = (uint8_t)((g_MirrorPPUCtrl1 & 0xfe) |
                                  (g_ScreenLeft_PageLoc & 1));
    g_ScrollIntervalTimer = 8;
}
