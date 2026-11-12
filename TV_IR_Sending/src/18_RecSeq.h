#pragma once

// =============================================================================
// 18_RecSeq.h — /recseq page: record an IR sequence for the TV IR RX
// =============================================================================
// For an RX that has no IR eye of its own: the remote (any: DSTV, TV, sound
// bar) is pointed at THIS board, and the RX keeps the recording and saves the
// sequence in its list. Running, editing and scheduling stay on the RX pages.
//   GET  /recseq          the page
//   GET  /api/rec         RX record state (JSON, polled by the page)
//   POST /api/rec/cmd     cmd = start|next|finish|retry|cancel|test,
//                         text (name / label), delay (ms) → cmdSeq as text
// Link side: 17_EspNow.cpp (EspNowRec*), packets in include/IrLink.h.
// =============================================================================

void RecSeqAddRoutes();   // from AddRoutes()
