#pragma once
#include "tc/limits.hpp"

/// Grapheme clusters by Qt's Unicode rules (QTextBoundaryFinder). Android's ICU may count some Bangla
/// conjuncts differently, which is why the receiver does not enforce the 10-character limit.
tc::limits::Segmenter qtSegmenter();
