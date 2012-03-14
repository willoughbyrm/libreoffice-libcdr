/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* libcdr
 * Version: MPL 1.1 / GPLv2+ / LGPLv2+
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License or as specified alternatively below. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * Major Contributor(s):
 * Copyright (C) 2012 Fridrich Strba <fridrich.strba@bluewin.ch>
 *
 *
 * All Rights Reserved.
 *
 * For minor contributions see the git repository.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPLv2+"), or
 * the GNU Lesser General Public License Version 2 or later (the "LGPLv2+"),
 * in which case the procdrns of the GPLv2+ or the LGPLv2+ are applicable
 * instead of those above.
 */

#include <string>
#include <string.h>
#include "CMXDocument.h"
#include "CMXParser.h"
#include "CDRSVGGenerator.h"
#include "CDRCollector.h"
#include "CDRZipStream.h"
#include "libcdr_utils.h"

/**
Analyzes the content of an input stream to see if it can be parsed
\param input The input stream
\return A value that indicates whether the content from the input
stream is a Corel Draw Document that libcdr is able to parse
*/
bool libcdr::CMXDocument::isSupported(WPXInputStream *input)
{
  input->seek(0, WPX_SEEK_SET);
  bool soFarSoGood = true;
  WPXString riff = readFourCC(input);
  if (riff != "RIFF")
    soFarSoGood = false;
  input->seek(4, WPX_SEEK_CUR);
  WPXString signature = readFourCC(input);
  if (soFarSoGood && signature.cstr()[0] != 'C' && signature.cstr()[0] != 'c')
    soFarSoGood = false;
  if (soFarSoGood && signature.cstr()[1] != 'M' && signature.cstr()[1] != 'm')
    soFarSoGood = false;
  if (soFarSoGood && signature.cstr()[2] != 'X' && signature.cstr()[2] != 'x')
    soFarSoGood = false;
  return soFarSoGood;
}

/**
Parses the input stream content. It will make callbacks to the functions provided by a
CDRPaintInterface class implementation when needed. This is often commonly called the
'main parsing routine'.
\param input The input stream
\param painter A CDRPainterInterface implementation
\return A value that indicates whether the parsing was successful
*/
bool libcdr::CMXDocument::parse(::WPXInputStream *input, libwpg::WPGPaintInterface *painter)
{
  input->seek(0, WPX_SEEK_SET);
  CDRCollector collector(painter);
  CMXParser parser(input, &collector);
  bool retVal = parser.parseRecords(input);
  return retVal;
}

/**
Parses the input stream content and generates a valid Scalable Vector Graphics
Provided as a convenience function for applications that support SVG internally.
\param input The input stream
\param output The output string whose content is the resulting SVG
\return A value that indicates whether the SVG generation was successful.
*/
bool libcdr::CMXDocument::generateSVG(::WPXInputStream *input, libcdr::CDRStringVector &output)
{
  libcdr::CDRSVGGenerator generator(output);
  bool result = libcdr::CMXDocument::parse(input, &generator);
  return result;
}

/* vim:set shiftwidth=2 softtabstop=2 expandtab: */