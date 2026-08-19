#pragma once

// Installs `sText` into StringPool slot `nIdx`, encoded the way the client expects.
//
// The pool stores the POINTER, not a copy, so the encoded bytes have to outlive the call. This
// keeps one stable buffer per index for exactly that reason -- monsterBookFoundIn.cpp swaps its
// slot back and forth between our wording and the stock text, and a single shared scratch buffer
// would leave the pool pointing at whichever string happened to be written last.
void SetStringPoolString(int nIdx, const char* sText);
