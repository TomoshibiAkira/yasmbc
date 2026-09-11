/* area_parser.c - Screen routine task 8 (main.asm:315-327, 1739-1764)
 *
 * NES executes AreaParserTaskControl once per NMI tick. Each call runs a
 * full TaskLoop round (8 sub-tasks: 2x AreaParserCore, 2x IncrementColumnPos,
 * 4x RenderAreaGraphics = 2 metatile columns), decrements ColumnSets, and
 * sets VRAM_Buffer_AddrCtrl=$06 so the next NMI flushes the rendered
 * columns. With ColumnSets=$0B the task spans 12 ticks (24 columns, 1.5
 * screens), incrementing DisableScreenFlag on every entry.
 */

#include <stddef.h>
#include "screen/screen.h"
#include "level/level.h"
#include "scroll.h"
#include "misc.h"
#include "constants/globals.h"

/* Reset parser column state; called by InitializeArea, which restarts
 * the renderer at column 0 with CurrentNTAddr=$2080 (main.asm:1434-1442). */
void AreaParser_Reset(void) {
    g_CurrentColumnPos = 0;
    g_AreaParserTaskNum = 0;
    {
        extern void Scroll_Reset(void);
        extern void Enemy_Reset(void);
        Scroll_Reset();
        Enemy_Reset();
        Misc_Reset();
    }
    Level_AreaObjectsReset();
}

static void AreaParser_IncrementColumnPos(void);

/* The initial-screen caller shares AreaParserTaskHandler's eight-entry
 * dispatch, but it has no scroll queue to select from.  RenderAreaGraphics
 * consumes the persistent CurrentPageLoc/CurrentColumnPos cursor directly;
 * its nametable base follows CurrentNTAddr ($0720/$0721), which is advanced
 * by each tile-column half in level.c. */
static void AreaParser_InitialRender(uint8_t right_half) {
    uint8_t page = g_CurrentPageLoc;
    uint8_t col = (uint8_t)(g_CurrentColumnPos & 0x0f);

    s_NTBaseOffset = (g_CurrentNTAddrHigh & 0x04) ? 0x0400 : 0x0000;
    if (right_half) {
        Level_RenderColumnRightHalf(page, col);
    } else {
        Level_RenderColumnLeftHalf(page, col);
    }
}

static void AreaParser_InitialTaskHandler(void) {
    uint8_t task;

    if (g_AreaParserTaskNum == 0)
        g_AreaParserTaskNum = 8;
    task = (uint8_t)(--g_AreaParserTaskNum);

    switch (task) {
        case 7: /* AreaParserCore */
            Level_AreaParserCore(g_CurrentPageLoc,
                                 (uint8_t)(g_CurrentColumnPos & 0x0f));
            break;
        case 6: /* RenderAreaGraphics, first tile-column half */
            AreaParser_InitialRender(0);
            break;
        case 5: /* RenderAreaGraphics, second tile-column half */
            AreaParser_InitialRender(1);
            break;
        case 4: /* IncrementColumnPos */
            AreaParser_IncrementColumnPos();
            break;
        case 3: /* AreaParserCore */
            Level_AreaParserCore(g_CurrentPageLoc,
                                 (uint8_t)(g_CurrentColumnPos & 0x0f));
            break;
        case 2: /* RenderAreaGraphics, first tile-column half */
            AreaParser_InitialRender(0);
            break;
        case 1: /* RenderAreaGraphics, second tile-column half */
            AreaParser_InitialRender(1);
            break;
        case 0: /* IncrementColumnPos */
            AreaParser_IncrementColumnPos();
            break;
        default:
            break;
    }
}

void AreaParserTaskControl(void) {
    /* inc DisableScreenFlag happens on every entry */
    g_DisableScreenFlag++;
    /* Rendered columns accumulate into the deferred queue; the next
     * NMI flush writes them to the PPU (NES VRAM_Buffer2 path). */
    Level_BeginDeferred();

    if (g_AreaDataPtr == NULL) {
        Level_Load(g_WorldNumber, g_AreaNumber);
    }

    /* TaskLoop: drain all eight AreaParserTasks.  This is two Core calls,
     * four RenderAreaGraphics halves, and two IncrementColumnPos calls,
     * producing the same two metatile columns while preserving the ASM
     * cursor and object-slot boundaries (main.asm:315-327, 1739-1779). */
    do {
        AreaParser_InitialTaskHandler();
    } while (g_AreaParserTaskNum != 0);

    /* AreaParserTaskHandler calls RenderAttributeTables after the final
     * IncrementColumnPos; keep the initial caller on the same producer
     * boundary before it decrements ColumnSets. */
    Level_RenderAttributeTables();

    /* dec ColumnSets; if expired, move to next screen routine task */
    {
        uint8_t colsets = g_ColumnSets--;
        if (colsets == 0) {
            /* wrapped below zero: parsing complete */
            g_ScreenRoutineTask++;
        }
    }

    /* OutputCol: flush rendered columns via buffer 2 next NMI */
    g_VRAM_Buffer_AddrCtrl = 0x06;
}

/* IncrementColumnPos (main.asm:1770-1788).  Both task-table entries 0 and 4
 * use this same routine: the parser cursor wraps the metatile-column nibble
 * into CurrentPageLoc, while BlockBufferColumnPos independently wraps in
 * the 32-column $0500/$05d0 ring. */
static void AreaParser_IncrementColumnPos(void) {
    g_CurrentColumnPos++;
    if ((g_CurrentColumnPos & 0x0f) == 0) {
        g_CurrentColumnPos = 0;
        g_CurrentPageLoc++;
    }
    g_BlockBufferColumnPos =
        (uint8_t)((g_BlockBufferColumnPos + 1) & 0x1f);
}

/* AreaParserTaskHandler (main.asm:1739-1779). This is the scroll-time
 * caller's real eight-entry dispatch; the legacy initial-screen TaskLoop
 * above remains isolated until the full MetatileBuffer/RendBBuf cluster is
 * translated. */
void AreaParserTaskHandler(void) {
    uint8_t task;

    if (g_AreaParserTaskNum == 0)
        g_AreaParserTaskNum = 8;
    task = (uint8_t)(--g_AreaParserTaskNum);

    switch (task) {
        case 7: /* AreaParserCore */
            Scroll_AreaParserSelectColumn(0);
            Level_AreaParserCore(g_CurrentPageLoc, g_CurrentColumnPos);
            break;
        case 6: /* RenderAreaGraphics, first/left tile column */
            Scroll_AreaParserSelectColumn(0);
            Scroll_AreaParserRender(0);
            break;
        case 5: /* RenderAreaGraphics, second/right tile column */
            Scroll_AreaParserSelectColumn(0);
            Scroll_AreaParserRender(1);
            break;
        case 4: /* IncrementColumnPos */
            /* IncrementColumnPos operates on the cursor left by the first
             * Core/render pair.  The NES task table does not select the
             * second deferred column before this call (main.asm:1757-1764);
             * selecting it here and then incrementing advances the parser
             * one metatile too early at a page boundary. */
            AreaParser_IncrementColumnPos();
            break;
        case 3: /* AreaParserCore */
            Scroll_AreaParserSelectColumn(1);
            Level_AreaParserCore(g_CurrentPageLoc, g_CurrentColumnPos);
            break;
        case 2: /* RenderAreaGraphics, first/left tile column */
            Scroll_AreaParserSelectColumn(1);
            Scroll_AreaParserRender(0);
            break;
        case 1: /* RenderAreaGraphics, second/right tile column */
            Scroll_AreaParserSelectColumn(1);
            Scroll_AreaParserRender(1);
            break;
        case 0: /* IncrementColumnPos */
            AreaParser_IncrementColumnPos();
            Level_RenderAttributeTables();
            Scroll_AreaParserComplete();
            g_VRAM_Buffer_AddrCtrl = 0x06;
            break;
        default:
            break;
    }

}
