#include "segmenter.hpp"

#include <QString>
#include <QTextBoundaryFinder>

tc::limits::Segmenter qtSegmenter() {
    return [](std::string_view utf8) {
        std::vector<std::size_t> ends;
        const QString text = QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size()));
        QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
        std::size_t bytes = 0;
        int prev = 0;
        for (int pos = finder.toNextBoundary(); pos != -1; pos = finder.toNextBoundary()) {
            bytes += static_cast<std::size_t>(text.mid(prev, pos - prev).toUtf8().size());
            ends.push_back(bytes);
            prev = pos;
        }
        return ends;
    };
}
