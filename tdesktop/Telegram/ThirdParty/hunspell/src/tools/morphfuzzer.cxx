/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

// Fuzzer for the morphology paths: analyze / stem / generate.
//
// These reach AffixMgr::compound_check_morph, which the spell/suggest
// fuzzers do not exercise.

#include <hunspell/hunspell.hxx>
#include <fstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const char* data, size_t size)
{
    if (size < 2)
        return 0;

    size_t word1len = static_cast<unsigned char>(data[0]);
    ++data;
    --size;
    if (word1len > size)
        return 0;
    std::string word1(data, word1len);
    data += word1len;
    size -= word1len;

    if (size < 1)
        return 0;
    size_t word2len = static_cast<unsigned char>(data[0]);
    ++data;
    --size;
    if (word2len > size)
        return 0;
    std::string word2(data, word2len);
    data += word2len;
    size -= word2len;

    size_t affsize = size / 2;
    std::ofstream aff("/tmp/morphtest.aff", std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
    aff.write(data, affsize);
    aff.close();

    size_t dicsize = size - affsize;
    std::ofstream dic("/tmp/morphtest.dic", std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
    dic.write(data + affsize, dicsize);
    dic.close();

    Hunspell dict("/tmp/morphtest.aff", "/tmp/morphtest.dic");

    dict.analyze(word1);
    dict.stem(word1);
    dict.generate(word1, word2);

    return 0;
}

#if 0
int main(void)
{
    // Local repro: read /tmp/morphtest.{aff,dic,word1,word2} on disk
    // and call the same paths the fuzzer drives.
    std::ifstream w1("/tmp/morphtest.word1", std::ios_base::in | std::ios_base::binary);
    std::ifstream w2("/tmp/morphtest.word2", std::ios_base::in | std::ios_base::binary);
    std::string word1((std::istreambuf_iterator<char>(w1)), std::istreambuf_iterator<char>());
    std::string word2((std::istreambuf_iterator<char>(w2)), std::istreambuf_iterator<char>());

    Hunspell dict("/tmp/morphtest.aff", "/tmp/morphtest.dic");
    dict.analyze(word1);
    dict.stem(word1);
    dict.generate(word1, word2);
    return 0;
}
#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
