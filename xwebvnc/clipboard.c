#include "libs/clipboard.h"
#include "dix.h"
#include <X11/Xatom.h>

Bool _server_owned_clipboard = FALSE;

static Atom gSelection = None;
static char *gData = NULL;
static int gLen = 0;
static Atom targets;
static Atom utf8;
static Atom text;
static Atom textPlain;
static Atom compoundText;

/* -------------------------------------------------- */
/* Set clipboard                                      */
/* -------------------------------------------------- */

void ServerSetClipboard(const char *data, int len) {
  if (gData)
    free(gData);
  gData = NULL;
  gLen = 0;

  if (!targets) {
    targets = MakeAtom("TARGETS", 7, TRUE);
    utf8 = MakeAtom("UTF8_STRING", 11, TRUE);
    text = MakeAtom("TEXT", 4, TRUE);
    gSelection = MakeAtom("CLIPBOARD", 9, TRUE);
    textPlain = MakeAtom("text/plain;charset=utf-8", 24, TRUE);
    compoundText = MakeAtom("COMPOUND_TEXT",13,TRUE);
  }

  if (!data || len <= 0)
    return;

  gData = malloc(len);
  if (!gData)
    return;

  memcpy(gData, data, len);
  gLen = len;

  _server_owned_clipboard = TRUE;
  ServerForceClearClientClipboard_();
}

/* -------------------------------------------------- */
/* Clear clipboard                                    */
/* -------------------------------------------------- */

void ServerClearClipboard(void) {
    if (gData)
      free(gData);
    gData = NULL;
    gLen = 0;
}

void ServerForceClearClientClipboard_(void) {
    Selection *pSel;
    int rc;

    rc = dixLookupSelection(&pSel, gSelection, serverClient, DixSetAttrAccess);
    if (rc == Success && pSel) {
        // Notify previous owner if it exists
        if (pSel->client && pSel->client != NullClient && !pSel->client->clientGone) {
            xEvent event;
            memset(&event, 0, sizeof(event));
            event.u.u.type = SelectionClear;
            event.u.selectionClear.time = currentTime.milliseconds;
            event.u.selectionClear.window = pSel->window;
            event.u.selectionClear.atom = pSel->selection;
            WriteEventsToClient(pSel->client, 1, &event);
        }

        // Clear ownership
        pSel->window = None;
        pSel->pWin   = NULL;
        pSel->client = NullClient;
        pSel->lastTimeChanged = currentTime;
    }
}

/* -------------------------------------------------- */
/* ConvertSelection handler                           */
/* -------------------------------------------------- */

int _server_clipboard(ClientPtr client, xConvertSelectionReq *stuff) {
  WindowPtr requestor;
  int rc;
  rc = dixLookupWindow(&requestor, stuff->requestor, client, DixWriteAccess);
  if (rc != Success)
    return rc;

  /* ============================= */
  /* 1️⃣ Handle TARGETS request     */
  /* ============================= */
  if (stuff->target == targets) {
    Atom supported[6];
    supported[0] = targets;
    supported[1] = utf8;
    supported[2] = text;
    supported[3] = XA_STRING;
    supported[4] = textPlain;
    supported[5]=compoundText;

    rc = dixChangeWindowProperty(serverClient, requestor, stuff->property,
                                     XA_ATOM, 32, PropModeReplace,
                                     6, supported, FALSE);

    if (rc != Success) {
      return rc;
    }
  }

  /* ============================= */
  /* 2️⃣ Handle TEXT request        */
  /* ============================= */
  else if (stuff->target == utf8 || stuff->target == XA_STRING || stuff->target == text || stuff->target == textPlain || stuff->target==compoundText) {
    rc = dixChangeWindowProperty(serverClient, requestor, stuff->property,
                                 stuff->target, 8, PropModeReplace, gLen, gData,
                                 FALSE);
    if (rc != Success)
      return rc;
  }

  /* ============================= */
  /* 3️⃣ Unsupported target         */
  /* ============================= */
  else {
    return Success;
  }

  /* ============================= */
  /* Send SelectionNotify          */
  /* ============================= */
  xEvent event;
  memset(&event, 0, sizeof(event));

  event.u.u.type = SelectionNotify;
  event.u.selectionNotify.time = stuff->time;
  event.u.selectionNotify.requestor = stuff->requestor;
  event.u.selectionNotify.selection = stuff->selection;
  event.u.selectionNotify.target = stuff->target;
  event.u.selectionNotify.property = stuff->property;

  WriteEventsToClient(client, 1, &event);
  return Success;
}
