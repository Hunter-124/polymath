.pragma library
//---------------------------------------------------------------------
// QR Code Generator for QML (Qt Quick) Canvas — byte-mode UTF-8 encoder
//
// Vendored from "qrcode-generator" by Kazuhiko Arase.
//   https://github.com/kazuhikoarase/qrcode-generator
//
// The MIT License (MIT)
//
// Copyright (c) 2009 Kazuhiko Arase
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// This is a faithful reproduction (byte-mode only) of the original
// algorithm. The Reed-Solomon / Galois-field math, version capacity
// tables, mask selection and BCH type info/version info are unchanged.
//---------------------------------------------------------------------

// ---- Error correction levels --------------------------------------
var QRErrorCorrectionLevel = { L: 1, M: 0, Q: 3, H: 2 };

// ---- Mask patterns ------------------------------------------------
var QRMaskPattern = {
    PATTERN000: 0, PATTERN001: 1, PATTERN010: 2, PATTERN011: 3,
    PATTERN100: 4, PATTERN101: 5, PATTERN110: 6, PATTERN111: 7
};

// ---- Mode ---------------------------------------------------------
var MODE_8BIT_BYTE = 1 << 2; // 0x4

// =====================================================================
// QRMath — Galois field GF(256)
// =====================================================================
var QRMath = (function() {
    var EXP_TABLE = new Array(256);
    var LOG_TABLE = new Array(256);
    var i;
    for (i = 0; i < 8; i += 1) {
        EXP_TABLE[i] = 1 << i;
    }
    for (i = 8; i < 256; i += 1) {
        EXP_TABLE[i] = EXP_TABLE[i - 4] ^ EXP_TABLE[i - 5] ^
                       EXP_TABLE[i - 6] ^ EXP_TABLE[i - 8];
    }
    for (i = 0; i < 255; i += 1) {
        LOG_TABLE[EXP_TABLE[i]] = i;
    }
    return {
        glog: function(n) {
            if (n < 1) {
                throw new Error('glog(' + n + ')');
            }
            return LOG_TABLE[n];
        },
        gexp: function(n) {
            while (n < 0) { n += 255; }
            while (n >= 256) { n -= 255; }
            return EXP_TABLE[n];
        }
    };
})();

// =====================================================================
// QRPolynomial
// =====================================================================
function qrPolynomial(num, shift) {
    if (typeof num.length === 'undefined') {
        throw new Error(num.length + '/' + shift);
    }
    var offset = 0;
    while (offset < num.length && num[offset] === 0) {
        offset += 1;
    }
    var _num = new Array(num.length - offset + shift);
    var i;
    for (i = 0; i < num.length - offset; i += 1) {
        _num[i] = num[i + offset];
    }

    var _this = {};
    _this.getAt = function(index) { return _num[index]; };
    _this.getLength = function() { return _num.length; };

    _this.multiply = function(e) {
        var n = new Array(_this.getLength() + e.getLength() - 1);
        var i, j;
        for (i = 0; i < n.length; i += 1) { n[i] = 0; }
        for (i = 0; i < _this.getLength(); i += 1) {
            for (j = 0; j < e.getLength(); j += 1) {
                n[i + j] ^= QRMath.gexp(
                    QRMath.glog(_this.getAt(i)) + QRMath.glog(e.getAt(j)));
            }
        }
        return qrPolynomial(n, 0);
    };

    _this.mod = function(e) {
        if (_this.getLength() - e.getLength() < 0) {
            return _this;
        }
        var ratio = QRMath.glog(_this.getAt(0)) - QRMath.glog(e.getAt(0));
        var num = new Array(_this.getLength());
        var i;
        for (i = 0; i < _this.getLength(); i += 1) {
            num[i] = _this.getAt(i);
        }
        for (i = 0; i < e.getLength(); i += 1) {
            num[i] ^= QRMath.gexp(QRMath.glog(e.getAt(i)) + ratio);
        }
        // recursive call
        return qrPolynomial(num, 0).mod(e);
    };

    return _this;
}

// =====================================================================
// QRRSBlock
// =====================================================================
var QRRSBlock = (function() {

    var RS_BLOCK_TABLE = [
        // L, M, Q, H — per QR version (1..40)
        [1, 26, 19], [1, 26, 16], [1, 26, 13], [1, 26, 9],
        [1, 44, 34], [1, 44, 28], [1, 44, 22], [1, 44, 16],
        [1, 70, 55], [1, 70, 44], [2, 35, 17], [2, 35, 13],
        [1, 100, 80], [2, 50, 32], [2, 50, 24], [4, 25, 9],
        [1, 134, 108], [2, 67, 43], [2, 33, 15, 2, 34, 16], [2, 33, 11, 2, 34, 12],
        [2, 86, 68], [4, 43, 27], [4, 43, 19], [4, 43, 15],
        [2, 98, 78], [4, 49, 31], [2, 32, 14, 4, 33, 15], [4, 39, 13, 1, 40, 14],
        [2, 121, 97], [2, 60, 38, 2, 61, 39], [4, 40, 18, 2, 41, 19], [4, 40, 14, 2, 41, 15],
        [2, 146, 116], [3, 58, 36, 2, 59, 37], [4, 36, 16, 4, 37, 17], [4, 36, 12, 4, 37, 13],
        [2, 86, 68, 2, 87, 69], [4, 69, 43, 1, 70, 44], [6, 43, 19, 2, 44, 20], [6, 43, 15, 2, 44, 16],
        [4, 101, 81], [1, 80, 50, 4, 81, 51], [4, 50, 22, 4, 51, 23], [3, 36, 12, 8, 37, 13],
        [2, 116, 92, 2, 117, 93], [6, 58, 36, 2, 59, 37], [4, 46, 20, 6, 47, 21], [7, 42, 14, 4, 43, 15],
        [4, 133, 107], [8, 59, 37, 1, 60, 38], [8, 44, 20, 4, 45, 21], [12, 33, 11, 4, 34, 12],
        [3, 145, 115, 1, 146, 116], [4, 64, 40, 5, 65, 41], [11, 36, 16, 5, 37, 17], [11, 36, 12, 5, 37, 13],
        [5, 109, 87, 1, 110, 88], [5, 65, 41, 5, 66, 42], [5, 54, 24, 7, 55, 25], [11, 36, 12, 7, 37, 13],
        [5, 122, 98, 1, 123, 99], [7, 73, 45, 3, 74, 46], [15, 43, 19, 2, 44, 20], [3, 45, 15, 13, 46, 16],
        [1, 135, 107, 5, 136, 108], [10, 74, 46, 1, 75, 47], [1, 50, 22, 15, 51, 23], [2, 42, 14, 17, 43, 15],
        [5, 150, 120, 1, 151, 121], [9, 69, 43, 4, 70, 44], [17, 50, 22, 1, 51, 23], [2, 42, 14, 19, 43, 15],
        [3, 141, 113, 4, 142, 114], [3, 70, 44, 11, 71, 45], [17, 47, 21, 4, 48, 22], [9, 39, 13, 16, 40, 14],
        [3, 135, 107, 5, 136, 108], [3, 67, 41, 13, 68, 42], [15, 54, 24, 5, 55, 25], [15, 43, 15, 10, 44, 16],
        [4, 144, 116, 4, 145, 117], [17, 68, 42], [17, 50, 22, 6, 51, 23], [19, 46, 16, 6, 47, 17],
        [2, 139, 111, 7, 140, 112], [17, 74, 46], [7, 54, 24, 16, 55, 25], [34, 37, 13],
        [4, 151, 121, 5, 152, 122], [4, 75, 47, 14, 76, 48], [11, 54, 24, 14, 55, 25], [16, 45, 15, 14, 46, 16],
        [6, 147, 117, 4, 148, 118], [6, 73, 45, 14, 74, 46], [11, 54, 24, 16, 55, 25], [30, 46, 16, 2, 47, 17],
        [8, 132, 106, 4, 133, 107], [8, 75, 47, 13, 76, 48], [7, 54, 24, 22, 55, 25], [22, 45, 15, 13, 46, 16],
        [10, 142, 114, 2, 143, 115], [19, 74, 46, 4, 75, 47], [28, 50, 22, 6, 51, 23], [33, 46, 16, 4, 47, 17],
        [8, 152, 122, 4, 153, 123], [22, 73, 45, 3, 74, 46], [8, 53, 23, 26, 54, 24], [12, 45, 15, 28, 46, 16],
        [3, 147, 117, 10, 148, 118], [3, 73, 45, 23, 74, 46], [4, 54, 24, 31, 55, 25], [11, 45, 15, 31, 46, 16],
        [7, 146, 116, 7, 147, 117], [21, 73, 45, 7, 74, 46], [1, 53, 23, 37, 54, 24], [19, 45, 15, 26, 46, 16],
        [5, 145, 115, 10, 146, 116], [19, 75, 47, 10, 76, 48], [15, 54, 24, 25, 55, 25], [23, 45, 15, 25, 46, 16],
        [13, 145, 115, 3, 146, 116], [2, 74, 46, 29, 75, 47], [42, 54, 24, 1, 55, 25], [23, 45, 15, 28, 46, 16],
        [17, 145, 115], [10, 74, 46, 23, 75, 47], [10, 54, 24, 35, 55, 25], [19, 45, 15, 35, 46, 16],
        [17, 145, 115, 1, 146, 116], [14, 74, 46, 21, 75, 47], [29, 54, 24, 19, 55, 25], [11, 45, 15, 46, 46, 16],
        [13, 145, 115, 6, 146, 116], [14, 74, 46, 23, 75, 47], [44, 54, 24, 7, 55, 25], [59, 46, 16, 1, 47, 17],
        [12, 151, 121, 7, 152, 122], [12, 75, 47, 26, 76, 48], [39, 54, 24, 14, 55, 25], [22, 45, 15, 41, 46, 16],
        [6, 151, 121, 14, 152, 122], [6, 75, 47, 34, 76, 48], [46, 54, 24, 10, 55, 25], [2, 45, 15, 64, 46, 16],
        [17, 152, 122, 4, 153, 123], [29, 74, 46, 14, 75, 47], [49, 54, 24, 10, 55, 25], [24, 45, 15, 46, 46, 16],
        [4, 152, 122, 18, 153, 123], [13, 74, 46, 32, 75, 47], [48, 54, 24, 14, 55, 25], [42, 45, 15, 32, 46, 16],
        [20, 147, 117, 4, 148, 118], [40, 75, 47, 7, 76, 48], [43, 54, 24, 22, 55, 25], [10, 45, 15, 67, 46, 16],
        [19, 148, 118, 6, 149, 119], [18, 75, 47, 31, 76, 48], [34, 54, 24, 34, 55, 25], [20, 45, 15, 61, 46, 16]
    ];

    var qrRSBlock = function(totalCount, dataCount) {
        return { totalCount: totalCount, dataCount: dataCount };
    };

    var _this = {};

    var getRsBlockTable = function(typeNumber, errorCorrectionLevel) {
        switch (errorCorrectionLevel) {
        case QRErrorCorrectionLevel.L:
            return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 0];
        case QRErrorCorrectionLevel.M:
            return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 1];
        case QRErrorCorrectionLevel.Q:
            return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 2];
        case QRErrorCorrectionLevel.H:
            return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 3];
        default:
            return undefined;
        }
    };

    _this.getRSBlocks = function(typeNumber, errorCorrectionLevel) {
        var rsBlock = getRsBlockTable(typeNumber, errorCorrectionLevel);
        if (typeof rsBlock === 'undefined') {
            throw new Error('bad rs block @ typeNumber:' + typeNumber +
                            '/errorCorrectionLevel:' + errorCorrectionLevel);
        }
        var length = rsBlock.length / 3;
        var list = [];
        var i, count, totalCount, dataCount, j;
        for (i = 0; i < length; i += 1) {
            count = rsBlock[i * 3 + 0];
            totalCount = rsBlock[i * 3 + 1];
            dataCount = rsBlock[i * 3 + 2];
            for (j = 0; j < count; j += 1) {
                list.push(qrRSBlock(totalCount, dataCount));
            }
        }
        return list;
    };

    return _this;
})();

// =====================================================================
// QRBitBuffer
// =====================================================================
function qrBitBuffer() {
    var _buffer = [];
    var _length = 0;

    var _this = {};

    _this.getBuffer = function() { return _buffer; };
    _this.getAt = function(index) {
        var bufIndex = Math.floor(index / 8);
        return ((_buffer[bufIndex] >>> (7 - index % 8)) & 1) === 1;
    };
    _this.put = function(num, length) {
        var i;
        for (i = 0; i < length; i += 1) {
            _this.putBit(((num >>> (length - i - 1)) & 1) === 1);
        }
    };
    _this.getLengthInBits = function() { return _length; };
    _this.putBit = function(bit) {
        var bufIndex = Math.floor(_length / 8);
        if (_buffer.length <= bufIndex) {
            _buffer.push(0);
        }
        if (bit) {
            _buffer[bufIndex] |= (0x80 >>> (_length % 8));
        }
        _length += 1;
    };

    return _this;
}

// =====================================================================
// qr8BitByte — UTF-8 byte-mode data segment
// =====================================================================
function qr8BitByte(data) {
    var _mode = MODE_8BIT_BYTE;
    var _bytes = stringToBytesUTF8(data);

    var _this = {};
    _this.getMode = function() { return _mode; };
    _this.getLength = function() { return _bytes.length; };
    _this.write = function(buffer) {
        var i;
        for (i = 0; i < _bytes.length; i += 1) {
            buffer.put(_bytes[i], 8);
        }
    };
    return _this;
}

// UTF-8 encode a JS string into an array of byte values (0..255).
function stringToBytesUTF8(s) {
    var bytes = [];
    var i, c;
    for (i = 0; i < s.length; i += 1) {
        c = s.charCodeAt(i);
        // Handle surrogate pairs -> full code point
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.length) {
            var c2 = s.charCodeAt(i + 1);
            if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                i += 1;
            }
        }
        if (c < 0x80) {
            bytes.push(c);
        } else if (c < 0x800) {
            bytes.push(0xC0 | (c >> 6));
            bytes.push(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            bytes.push(0xE0 | (c >> 12));
            bytes.push(0x80 | ((c >> 6) & 0x3F));
            bytes.push(0x80 | (c & 0x3F));
        } else {
            bytes.push(0xF0 | (c >> 18));
            bytes.push(0x80 | ((c >> 12) & 0x3F));
            bytes.push(0x80 | ((c >> 6) & 0x3F));
            bytes.push(0x80 | (c & 0x3F));
        }
    }
    return bytes;
}

// =====================================================================
// QRUtil — type info, version info, masks, lost point, generator poly
// =====================================================================
var QRUtil = (function() {

    var PATTERN_POSITION_TABLE = [
        [], [6, 18], [6, 22], [6, 26], [6, 30], [6, 34],
        [6, 22, 38], [6, 24, 42], [6, 26, 46], [6, 28, 50], [6, 30, 54],
        [6, 32, 58], [6, 34, 62], [6, 26, 46, 66], [6, 26, 48, 70],
        [6, 26, 50, 74], [6, 30, 54, 78], [6, 30, 56, 82], [6, 30, 58, 86],
        [6, 34, 62, 90], [6, 28, 50, 72, 94], [6, 26, 50, 74, 98],
        [6, 30, 54, 78, 102], [6, 28, 54, 80, 106], [6, 32, 58, 84, 110],
        [6, 30, 58, 86, 114], [6, 34, 62, 90, 118], [6, 26, 50, 74, 98, 122],
        [6, 30, 54, 78, 102, 126], [6, 26, 52, 78, 104, 130],
        [6, 30, 56, 82, 108, 134], [6, 34, 60, 86, 112, 138],
        [6, 30, 58, 86, 114, 142], [6, 34, 62, 90, 118, 146],
        [6, 30, 54, 78, 102, 126, 150], [6, 24, 50, 76, 102, 128, 154],
        [6, 28, 54, 80, 106, 132, 158], [6, 32, 58, 84, 110, 136, 162],
        [6, 26, 54, 82, 110, 138, 166], [6, 30, 58, 86, 114, 142, 170]
    ];

    var G15 = (1 << 10) | (1 << 8) | (1 << 5) | (1 << 4) | (1 << 2) | (1 << 1) | (1 << 0);
    var G18 = (1 << 12) | (1 << 11) | (1 << 10) | (1 << 9) | (1 << 8) | (1 << 5) | (1 << 2) | (1 << 0);
    var G15_MASK = (1 << 14) | (1 << 12) | (1 << 10) | (1 << 4) | (1 << 1);

    var _this = {};

    var getBCHDigit = function(data) {
        var digit = 0;
        while (data !== 0) {
            digit += 1;
            data >>>= 1;
        }
        return digit;
    };

    _this.getBCHTypeInfo = function(data) {
        var d = data << 10;
        while (getBCHDigit(d) - getBCHDigit(G15) >= 0) {
            d ^= (G15 << (getBCHDigit(d) - getBCHDigit(G15)));
        }
        return ((data << 10) | d) ^ G15_MASK;
    };

    _this.getBCHTypeNumber = function(data) {
        var d = data << 12;
        while (getBCHDigit(d) - getBCHDigit(G18) >= 0) {
            d ^= (G18 << (getBCHDigit(d) - getBCHDigit(G18)));
        }
        return (data << 12) | d;
    };

    _this.getPatternPosition = function(typeNumber) {
        return PATTERN_POSITION_TABLE[typeNumber - 1];
    };

    _this.getMaskFunction = function(maskPattern) {
        switch (maskPattern) {
        case QRMaskPattern.PATTERN000: return function(i, j) { return (i + j) % 2 === 0; };
        case QRMaskPattern.PATTERN001: return function(i, j) { return i % 2 === 0; };
        case QRMaskPattern.PATTERN010: return function(i, j) { return j % 3 === 0; };
        case QRMaskPattern.PATTERN011: return function(i, j) { return (i + j) % 3 === 0; };
        case QRMaskPattern.PATTERN100: return function(i, j) { return (Math.floor(i / 2) + Math.floor(j / 3)) % 2 === 0; };
        case QRMaskPattern.PATTERN101: return function(i, j) { return (i * j) % 2 + (i * j) % 3 === 0; };
        case QRMaskPattern.PATTERN110: return function(i, j) { return ((i * j) % 2 + (i * j) % 3) % 2 === 0; };
        case QRMaskPattern.PATTERN111: return function(i, j) { return ((i * j) % 3 + (i + j) % 2) % 2 === 0; };
        default: throw new Error('bad maskPattern:' + maskPattern);
        }
    };

    _this.getErrorCorrectPolynomial = function(errorCorrectLength) {
        var a = qrPolynomial([1], 0);
        var i;
        for (i = 0; i < errorCorrectLength; i += 1) {
            a = a.multiply(qrPolynomial([1, QRMath.gexp(i)], 0));
        }
        return a;
    };

    _this.getLengthInBits = function(mode, type) {
        if (1 <= type && type < 10) {
            // 1 - 9
            switch (mode) {
            case MODE_8BIT_BYTE: return 8;
            default: throw new Error('mode:' + mode);
            }
        } else if (type < 27) {
            // 10 - 26
            switch (mode) {
            case MODE_8BIT_BYTE: return 16;
            default: throw new Error('mode:' + mode);
            }
        } else if (type < 41) {
            // 27 - 40
            switch (mode) {
            case MODE_8BIT_BYTE: return 16;
            default: throw new Error('mode:' + mode);
            }
        } else {
            throw new Error('type:' + type);
        }
    };

    _this.getLostPoint = function(qrcode) {
        var moduleCount = qrcode.getModuleCount();
        var lostPoint = 0;
        var row, col;

        // LEVEL1
        for (row = 0; row < moduleCount; row += 1) {
            for (col = 0; col < moduleCount; col += 1) {
                var sameCount = 0;
                var dark = qrcode.isDark(row, col);
                var r, c;
                for (r = -1; r <= 1; r += 1) {
                    if (row + r < 0 || moduleCount <= row + r) { continue; }
                    for (c = -1; c <= 1; c += 1) {
                        if (col + c < 0 || moduleCount <= col + c) { continue; }
                        if (r === 0 && c === 0) { continue; }
                        if (dark === qrcode.isDark(row + r, col + c)) {
                            sameCount += 1;
                        }
                    }
                }
                if (sameCount > 5) {
                    lostPoint += (3 + sameCount - 5);
                }
            }
        }

        // LEVEL2
        for (row = 0; row < moduleCount - 1; row += 1) {
            for (col = 0; col < moduleCount - 1; col += 1) {
                var count = 0;
                if (qrcode.isDark(row, col)) { count += 1; }
                if (qrcode.isDark(row + 1, col)) { count += 1; }
                if (qrcode.isDark(row, col + 1)) { count += 1; }
                if (qrcode.isDark(row + 1, col + 1)) { count += 1; }
                if (count === 0 || count === 4) {
                    lostPoint += 3;
                }
            }
        }

        // LEVEL3
        for (row = 0; row < moduleCount; row += 1) {
            for (col = 0; col < moduleCount - 6; col += 1) {
                if (qrcode.isDark(row, col) &&
                    !qrcode.isDark(row, col + 1) &&
                    qrcode.isDark(row, col + 2) &&
                    qrcode.isDark(row, col + 3) &&
                    qrcode.isDark(row, col + 4) &&
                    !qrcode.isDark(row, col + 5) &&
                    qrcode.isDark(row, col + 6)) {
                    lostPoint += 40;
                }
            }
        }
        for (col = 0; col < moduleCount; col += 1) {
            for (row = 0; row < moduleCount - 6; row += 1) {
                if (qrcode.isDark(row, col) &&
                    !qrcode.isDark(row + 1, col) &&
                    qrcode.isDark(row + 2, col) &&
                    qrcode.isDark(row + 3, col) &&
                    qrcode.isDark(row + 4, col) &&
                    !qrcode.isDark(row + 5, col) &&
                    qrcode.isDark(row + 6, col)) {
                    lostPoint += 40;
                }
            }
        }

        // LEVEL4
        var darkCount = 0;
        for (col = 0; col < moduleCount; col += 1) {
            for (row = 0; row < moduleCount; row += 1) {
                if (qrcode.isDark(row, col)) {
                    darkCount += 1;
                }
            }
        }
        var ratio = Math.abs(100 * darkCount / moduleCount / moduleCount - 50) / 5;
        lostPoint += ratio * 10;

        return lostPoint;
    };

    return _this;
})();

// =====================================================================
// qrcode — the model
// =====================================================================
function qrcode(typeNumber, errorCorrectionLevel) {

    var PAD0 = 0xEC;
    var PAD1 = 0x11;

    var _typeNumber = typeNumber;
    var _errorCorrectionLevel = QRErrorCorrectionLevel[errorCorrectionLevel];
    var _modules = null;
    var _moduleCount = 0;
    var _dataCache = null;
    var _dataList = [];

    var _this = {};

    var makeImpl = function(test, maskPattern) {

        _moduleCount = _typeNumber * 4 + 17;
        _modules = (function(moduleCount) {
            var modules = new Array(moduleCount);
            var row, col;
            for (row = 0; row < moduleCount; row += 1) {
                modules[row] = new Array(moduleCount);
                for (col = 0; col < moduleCount; col += 1) {
                    modules[row][col] = null;
                }
            }
            return modules;
        })(_moduleCount);

        setupPositionProbePattern(0, 0);
        setupPositionProbePattern(_moduleCount - 7, 0);
        setupPositionProbePattern(0, _moduleCount - 7);
        setupPositionAdjustPattern();
        setupTimingPattern();
        setupTypeInfo(test, maskPattern);

        if (_typeNumber >= 7) {
            setupTypeNumber(test);
        }

        if (_dataCache === null) {
            _dataCache = createData(_typeNumber, _errorCorrectionLevel, _dataList);
        }

        mapData(_dataCache, maskPattern);
    };

    var setupPositionProbePattern = function(row, col) {
        var r, c;
        for (r = -1; r <= 7; r += 1) {
            if (row + r <= -1 || _moduleCount <= row + r) { continue; }
            for (c = -1; c <= 7; c += 1) {
                if (col + c <= -1 || _moduleCount <= col + c) { continue; }
                if ((0 <= r && r <= 6 && (c === 0 || c === 6)) ||
                    (0 <= c && c <= 6 && (r === 0 || r === 6)) ||
                    (2 <= r && r <= 4 && 2 <= c && c <= 4)) {
                    _modules[row + r][col + c] = true;
                } else {
                    _modules[row + r][col + c] = false;
                }
            }
        }
    };

    var setupPositionAdjustPattern = function() {
        var pos = QRUtil.getPatternPosition(_typeNumber);
        var i, j;
        for (i = 0; i < pos.length; i += 1) {
            for (j = 0; j < pos.length; j += 1) {
                var row = pos[i];
                var col = pos[j];
                if (_modules[row][col] !== null) {
                    continue;
                }
                var r, c;
                for (r = -2; r <= 2; r += 1) {
                    for (c = -2; c <= 2; c += 1) {
                        if (r === -2 || r === 2 || c === -2 || c === 2 ||
                            (r === 0 && c === 0)) {
                            _modules[row + r][col + c] = true;
                        } else {
                            _modules[row + r][col + c] = false;
                        }
                    }
                }
            }
        }
    };

    var setupTimingPattern = function() {
        var r, c;
        for (r = 8; r < _moduleCount - 8; r += 1) {
            if (_modules[r][6] !== null) {
                continue;
            }
            _modules[r][6] = (r % 2 === 0);
        }
        for (c = 8; c < _moduleCount - 8; c += 1) {
            if (_modules[6][c] !== null) {
                continue;
            }
            _modules[6][c] = (c % 2 === 0);
        }
    };

    var setupTypeNumber = function(test) {
        var bits = QRUtil.getBCHTypeNumber(_typeNumber);
        var i, mod;
        for (i = 0; i < 18; i += 1) {
            mod = (!test && ((bits >> i) & 1) === 1);
            _modules[Math.floor(i / 3)][i % 3 + _moduleCount - 8 - 3] = mod;
        }
        for (i = 0; i < 18; i += 1) {
            mod = (!test && ((bits >> i) & 1) === 1);
            _modules[i % 3 + _moduleCount - 8 - 3][Math.floor(i / 3)] = mod;
        }
    };

    var setupTypeInfo = function(test, maskPattern) {
        var data = (_errorCorrectionLevel << 3) | maskPattern;
        var bits = QRUtil.getBCHTypeInfo(data);
        var i, mod;

        // vertical
        for (i = 0; i < 15; i += 1) {
            mod = (!test && ((bits >> i) & 1) === 1);
            if (i < 6) {
                _modules[i][8] = mod;
            } else if (i < 8) {
                _modules[i + 1][8] = mod;
            } else {
                _modules[_moduleCount - 15 + i][8] = mod;
            }
        }

        // horizontal
        for (i = 0; i < 15; i += 1) {
            mod = (!test && ((bits >> i) & 1) === 1);
            if (i < 8) {
                _modules[8][_moduleCount - i - 1] = mod;
            } else if (i < 9) {
                _modules[8][15 - i - 1 + 1] = mod;
            } else {
                _modules[8][15 - i - 1] = mod;
            }
        }

        // fixed module
        _modules[_moduleCount - 8][8] = (!test);
    };

    var mapData = function(data, maskPattern) {
        var inc = -1;
        var row = _moduleCount - 1;
        var bitIndex = 7;
        var byteIndex = 0;
        var maskFunc = QRUtil.getMaskFunction(maskPattern);
        var col;

        for (col = _moduleCount - 1; col > 0; col -= 2) {
            if (col === 6) { col -= 1; }
            while (true) {
                var c;
                for (c = 0; c < 2; c += 1) {
                    if (_modules[row][col - c] === null) {
                        var dark = false;
                        if (byteIndex < data.length) {
                            dark = (((data[byteIndex] >>> bitIndex) & 1) === 1);
                        }
                        var mask = maskFunc(row, col - c);
                        if (mask) {
                            dark = !dark;
                        }
                        _modules[row][col - c] = dark;
                        bitIndex -= 1;
                        if (bitIndex === -1) {
                            byteIndex += 1;
                            bitIndex = 7;
                        }
                    }
                }
                row += inc;
                if (row < 0 || _moduleCount <= row) {
                    row -= inc;
                    inc = -inc;
                    break;
                }
            }
        }
    };

    var createBytes = function(buffer, rsBlocks) {
        var offset = 0;
        var maxDcCount = 0;
        var maxEcCount = 0;
        var dcdata = new Array(rsBlocks.length);
        var ecdata = new Array(rsBlocks.length);
        var r, i;

        for (r = 0; r < rsBlocks.length; r += 1) {
            var dcCount = rsBlocks[r].dataCount;
            var ecCount = rsBlocks[r].totalCount - dcCount;

            maxDcCount = Math.max(maxDcCount, dcCount);
            maxEcCount = Math.max(maxEcCount, ecCount);

            dcdata[r] = new Array(dcCount);
            for (i = 0; i < dcdata[r].length; i += 1) {
                dcdata[r][i] = 0xff & buffer.getBuffer()[i + offset];
            }
            offset += dcCount;

            var rsPoly = QRUtil.getErrorCorrectPolynomial(ecCount);
            var rawPoly = qrPolynomial(dcdata[r], rsPoly.getLength() - 1);

            var modPoly = rawPoly.mod(rsPoly);
            ecdata[r] = new Array(rsPoly.getLength() - 1);
            for (i = 0; i < ecdata[r].length; i += 1) {
                var modIndex = i + modPoly.getLength() - ecdata[r].length;
                ecdata[r][i] = (modIndex >= 0) ? modPoly.getAt(modIndex) : 0;
            }
        }

        var totalCodeCount = 0;
        for (i = 0; i < rsBlocks.length; i += 1) {
            totalCodeCount += rsBlocks[i].totalCount;
        }

        var data = new Array(totalCodeCount);
        var index = 0;

        for (i = 0; i < maxDcCount; i += 1) {
            for (r = 0; r < rsBlocks.length; r += 1) {
                if (i < dcdata[r].length) {
                    data[index] = dcdata[r][i];
                    index += 1;
                }
            }
        }

        for (i = 0; i < maxEcCount; i += 1) {
            for (r = 0; r < rsBlocks.length; r += 1) {
                if (i < ecdata[r].length) {
                    data[index] = ecdata[r][i];
                    index += 1;
                }
            }
        }

        return data;
    };

    var createData = function(typeNumber, errorCorrectionLevel, dataList) {
        var rsBlocks = QRRSBlock.getRSBlocks(typeNumber, errorCorrectionLevel);
        var buffer = qrBitBuffer();
        var i, data;

        for (i = 0; i < dataList.length; i += 1) {
            data = dataList[i];
            buffer.put(data.getMode(), 4);
            buffer.put(data.getLength(),
                       QRUtil.getLengthInBits(data.getMode(), typeNumber));
            data.write(buffer);
        }

        // calc num max data.
        var totalDataCount = 0;
        for (i = 0; i < rsBlocks.length; i += 1) {
            totalDataCount += rsBlocks[i].dataCount;
        }

        if (buffer.getLengthInBits() > totalDataCount * 8) {
            throw new Error('code length overflow. (' +
                buffer.getLengthInBits() + '>' + totalDataCount * 8 + ')');
        }

        // end code
        if (buffer.getLengthInBits() + 4 <= totalDataCount * 8) {
            buffer.put(0, 4);
        }

        // padding
        while (buffer.getLengthInBits() % 8 !== 0) {
            buffer.putBit(false);
        }

        // padding
        while (true) {
            if (buffer.getLengthInBits() >= totalDataCount * 8) {
                break;
            }
            buffer.put(PAD0, 8);
            if (buffer.getLengthInBits() >= totalDataCount * 8) {
                break;
            }
            buffer.put(PAD1, 8);
        }

        return createBytes(buffer, rsBlocks);
    };

    _this.addData = function(data) {
        var newData = qr8BitByte(data);
        _dataList.push(newData);
        _dataCache = null;
    };

    _this.isDark = function(row, col) {
        if (row < 0 || _moduleCount <= row || col < 0 || _moduleCount <= col) {
            throw new Error(row + ',' + col);
        }
        return _modules[row][col];
    };

    _this.getModuleCount = function() {
        return _moduleCount;
    };

    _this.make = function() {
        if (_typeNumber < 1) {
            // auto-select smallest version that fits
            var typeNumber = 1;
            for (typeNumber = 1; typeNumber < 41; typeNumber += 1) {
                var rsBlocks = QRRSBlock.getRSBlocks(typeNumber, _errorCorrectionLevel);
                var buffer = qrBitBuffer();
                var i;
                for (i = 0; i < _dataList.length; i += 1) {
                    var data = _dataList[i];
                    buffer.put(data.getMode(), 4);
                    buffer.put(data.getLength(),
                               QRUtil.getLengthInBits(data.getMode(), typeNumber));
                    data.write(buffer);
                }
                var totalDataCount = 0;
                for (i = 0; i < rsBlocks.length; i += 1) {
                    totalDataCount += rsBlocks[i].dataCount;
                }
                if (buffer.getLengthInBits() <= totalDataCount * 8) {
                    break;
                }
            }
            _typeNumber = typeNumber;
        }
        makeImpl(false, getBestMaskPattern());
    };

    var getBestMaskPattern = function() {
        var minLostPoint = 0;
        var pattern = 0;
        var i;
        for (i = 0; i < 8; i += 1) {
            makeImpl(true, i);
            var lostPoint = QRUtil.getLostPoint(_this);
            if (i === 0 || minLostPoint > lostPoint) {
                minLostPoint = lostPoint;
                pattern = i;
            }
        }
        return pattern;
    };

    return _this;
}

// =====================================================================
// Public API
// =====================================================================
//
// encode(text, ecc) -> { size: int, isDark: function(row,col)->bool, modules: [[bool]] }
//
//   text : the string payload (UTF-8, byte mode). ~120-180 byte JSON is fine.
//   ecc  : "L" | "M" | "Q" | "H"   (default "M")
//
// `size` is the module count: 21 + 4*(version-1) -> 21,25,...,177.
// `isDark(row, col)` returns true for dark modules.
// `modules` is a [size][size] array of booleans (true = dark).
// ---------------------------------------------------------------------
function encode(text, ecc) {
    if (typeof ecc === 'undefined' || ecc === null) {
        ecc = 'M';
    }
    ecc = ('' + ecc).toUpperCase();
    if (ecc !== 'L' && ecc !== 'M' && ecc !== 'Q' && ecc !== 'H') {
        throw new Error('bad ecc level: ' + ecc + ' (expected L|M|Q|H)');
    }

    // typeNumber 0 -> auto-select version
    var qr = qrcode(0, ecc);
    qr.addData('' + text);
    qr.make();

    var size = qr.getModuleCount();

    // Materialize the modules into a plain 2D boolean array (convenient
    // and avoids repeated function-call overhead inside Canvas paint).
    var modules = new Array(size);
    var r, c;
    for (r = 0; r < size; r += 1) {
        modules[r] = new Array(size);
        for (c = 0; c < size; c += 1) {
            modules[r][c] = qr.isDark(r, c) === true;
        }
    }

    return {
        size: size,
        isDark: function(row, col) {
            if (row < 0 || row >= size || col < 0 || col >= size) {
                return false;
            }
            return modules[row][col];
        },
        modules: modules
    };
}
