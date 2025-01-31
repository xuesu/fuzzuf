/*
 * fuzzuf
 * Copyright (C) 2021 Ricerca Security
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */
/**
 * @file GetHash.cpp
 * @brief Calculate SHA1 hash
 * @author Ricerca Security <fuzzuf-dev@ricsec.co.jp>
 */
#include <cryptopp/sha.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include "fuzzuf/utils/get_hash.hpp"
std::string GetSHA1HashFromFile(std::string path, u32 len) {
    int fd = Util::OpenFile(path, O_RDONLY);
    u8 *buf = new u8[len];
    Util::ReadFile(fd, buf, len);
    Util::CloseFile(fd);

    CryptoPP::SHA1 sha1;
    std::string hash = "";

    CryptoPP::StringSource(buf, len, true, new CryptoPP::HashFilter(sha1, new CryptoPP::HexEncoder(new CryptoPP::StringSink(hash))));
    delete[] buf;
    return hash;
}
