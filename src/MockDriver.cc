/* Copyright (c) 2010 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright
 * notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "MockDriver.h"

#include "TestUtil.h"

namespace RAMCloud {

/**
 * Construct a MockDriver which does not include the header in the outputLog.
 */
MockDriver::MockDriver()
            : headerToString(0)
            , inputReceived(0)
            , outputLog()
            , sendPacketCount(0)
            , tryRecvPacketCount(0)
            , releaseCount(0)
{
}

/**
 * Construct a MockDriver with a custom serializer for the opaque header in
 * the outputLog.
 *
 * \param headerToString
 *      A pointer to a function which serializes a Header into a format
 *      for prefixing packets in the outputLog.
 */
MockDriver::MockDriver(HeaderToString headerToString)
            : headerToString(headerToString)
            , inputReceived(0)
            , outputLog()
            , sendPacketCount(0)
            , tryRecvPacketCount(0)
            , releaseCount(0)
{
}

/**
 * Counts number of times release is called to allow unit tests to check
 * that Driver resources are properly reclaimed.
 *
 * See Driver::release().
 */
void
MockDriver::release(char *payload, uint32_t len)
{
    releaseCount++;
}

/**
 * Counts number of times sendPacket for unit tests and logs the sent
 * packet to outputLog.
 *
 * See Driver::release().
 */
void
MockDriver::sendPacket(const sockaddr *addr,
                       socklen_t addrlen,
                       void *header,
                       uint32_t headerLen,
                       Buffer::Iterator *payload)
{
    sendPacketCount++;

    if (outputLog.length() != 0)
        outputLog.append(" | ");

    // TODO(stutsman) Append target address as well once we settle on
    // format of addresses in the system?

    if (headerToString && header) {
        outputLog += headerToString(header, headerLen);
        outputLog += " ";
    }

    if (!payload)
        return;

    uint32_t length = payload->getTotalLength();
    char buf[length];

    uint32_t off = 0;
    while (!payload->isDone()) {
        uint32_t l = payload->getLength();
        memcpy(&buf[off],
               const_cast<void*>(payload->getData()), l);
        off += l;
        payload->next();
    }

    uint32_t take = 10;
    if (length < take) {
        bufToString(buf, length, &outputLog);
    } else {
        bufToString(buf, take, &outputLog);
        char tmp[50];
        snprintf(tmp, sizeof(tmp), " (+%u more)", length - take);
        outputLog += tmp;
    }
}

/**
 * Wait for an incoming packet. This is a fake method that uses
 * a message explicitly provided by the test, or an empty
 * buffer if none was provided.
 *
 * See Driver::tryRecvPacket.
 */
bool
MockDriver::tryRecvPacket(Driver::Received *received)
{
    tryRecvPacketCount++;

    if (!inputReceived)
        return false;

    received->addrlen = inputReceived->addrlen;
    // dangerous, but only used in testing
    received->payload = inputReceived->payload;
    received->len = inputReceived->len;
    received->driver = this;

    inputReceived = 0;

    return true;
}

/**
 * This method is invoked by tests to provide a Received that will
 * be used to synthesize an input message the next time one is
 * needed (such as for a packet).
 *
 * \param received
 *      A Driver::Received to return from the next call to
 *      tryRecvPacket(); probably a MockReceived, even.
 */
void
MockDriver::setInput(Driver::Received* received)
{
    inputReceived = received;
}

/**
 * Append a printable representation of the contents of the buffer
 * to a string.
 *
 * \param buffer
 *      Convert the contents of this to ASCII.
 * \param[out] s
 *      Append the converted value here. The output format is intended
 *      to simplify testing: things that look like strings are output
 *      that way, and everything else is output as 4-byte decimal integers.
 */
void
MockDriver::bufferToString(Buffer *buffer, string* const s) {
    uint32_t length = buffer->getTotalLength();
    char buf[length];
    buffer->copy(0, length, buf);
    bufToString(buf, length, s);
}

/**
 * Fill in the contents of the buffer from a textual description.
 *
 * \param s
 *      Describes what to put in the buffer. Consists of one or more
 *      substrings separated by spaces. If a substring starts with a
 *      digit or "-" is assumed to be a decimal number, which is converted
 *      to a 4-byte signed integer in the buffer. Otherwise the
 *      characters of the substrate are appended to the buffer, with
 *      an additional null terminating character.
 * \param[out] buffer
 *      Where to store the results of conversion; any existing contents
 *      are discarded.
 */
void
MockDriver::stringToBuffer(const char* s, Buffer *buffer) {
    buffer->truncateFront(buffer->getTotalLength());

    uint32_t i, length;
    length = strlen(s);
    for (i = 0; i < length; ) {
        char c = s[i];
        if ((c == '0') && (s[i+1] == 'x')) {
            // Hexadecimal number
            int value = 0;
            i += 2;
            while (i < length) {
                char c = s[i];
                i++;
                if (c == ' ') {
                    break;
                }
                if (c <= '9') {
                    value = 16*value + (c - '0');
                } else if ((c >= 'a') && (c <= 'f')) {
                    value = 16*value + 10 + (c - 'a');
                } else {
                    value = 16*value + 10 + (c - 'A');
                }
            }
            *(new(buffer, APPEND) int32_t) = value;
        } else if ((c == '-') || ((c >= '0') && (c <= '9'))) {
            // Decimal number
            int value = 0;
            int sign = (c == '-') ? -1 : 1;
            if (c == '-') {
                sign = -1;
                i++;
            }
            while (i < length) {
                char c = s[i];
                i++;
                if (c == ' ') {
                    break;
                }
                value = 10*value + (c - '0');
            }
            *(new(buffer, APPEND) int32_t) = value * sign;
        } else {
            // String
            while (i < length) {
                char c = s[i];
                i++;
                if (c == ' ') {
                    break;
                }
                *(new(buffer, APPEND) char) = c;
            }
            *(new(buffer, APPEND) char) = 0;
        }
    }
    string s2;
    bufferToString(buffer, &s2);
}

/**
 * Create a string representation of a range of bytes.
 *
 * \param buf
 *      The start of the region to stringify.
 * \param bufLen
 *      The number of bytes to stringify.
 */
string
MockDriver::bufToHex(const void* buf, uint32_t bufLen)
{
    const unsigned char* cbuf = reinterpret_cast<const unsigned char*>(buf);
    string s;
    char c[4];

    for (uint32_t i = 0; i < bufLen; i++) {
        snprintf(c, sizeof(c), "%02x ", cbuf[i]);
        s += c;
    }

    return s;
}

}  // namespace RAMCloud