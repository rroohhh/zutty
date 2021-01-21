/* This file is part of Zutty.
 * Copyright (C) 2020 Tom Szilagyi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the file LICENSE for the full license.
 */

#include "fontpack.h"
#include "log.h"

#include <ftw.h>
//#include <stdio.h> // DEBUG
#include <string.h>
#include <strings.h>

namespace {

   struct SearchState
   {
      // input
      const char* fontname = nullptr;
      size_t fontnamelen = 0;

      // output
      int level = 0;
      std::string ext;
      std::string regular;
      std::string bold;
      std::string italic;
      std::string boldItalic;
   };
   SearchState sstate;

   int
   saveCandidate (const char* fpath, const char* ext, int level,
                  const char* variant, std::string& dest)
   {
      logT << variant << ": " << fpath << std::endl;
      if (sstate.ext != "" && sstate.ext != ext)
      {
         logT << "Rejecting candidate because its extension: '" << ext
              << "' does not match the other(s): '" << sstate.ext << "'"
              << std::endl;
         return 1;
      }
      dest = fpath;
      sstate.ext = ext;
      sstate.level = level;
      return 0;
   }

   int
   fontFileFilter (const char* fpath, const struct stat* sb,
                   int tflag, struct FTW* ftwbuf)
   {
      // If we have just emerged from a directory where fonts were found,
      // time to call it a day.
      if (tflag == FTW_D &&
          sstate.level > 0 && ftwbuf->level == sstate.level - 1 &&
          sstate.regular.size () > 0)
         return 1;

      // In a less ideal case, where some candidates were found in this dir,
      // but no regular variant, clear partial results and proceed.
      if (tflag == FTW_D && sstate.level > 0)
      {
         logT << "Some candidates found but no regular variant; continuing"
              << std::endl;
         sstate.level = 0;
         sstate.ext = "";
         sstate.regular = "";
         sstate.bold = "";
         sstate.italic = "";
         sstate.boldItalic = "";
         return 0;
      }

      // Filter by file type - only regular files
      if (tflag != FTW_F)
         return 0;

      // Filter by extension
      const char* fname = fpath + ftwbuf->base;
      const char* ext = strrchr (fname, '.');
      if (!ext)
         return 0;
      if (strcasecmp (ext, ".gz") == 0 && ext > fname)
         do
            --ext;
         while (ext > fname && ext [0] != '.');

      if (strcasecmp (ext, ".ttf") != 0 &&
          strcasecmp (ext, ".otf") != 0 &&
          strcasecmp (ext, ".pcf.gz") != 0)
         return 0;

      // Filter by font name
      if (strncasecmp (fname, sstate.fontname, sstate.fontnamelen) != 0)
         return 0;

      // At this point we only need to consider the mid part between font name
      // and extension. This is either matched to one of the face variants,
      // or we reject the file.
      const char* mid = fname + sstate.fontnamelen;
      size_t midlen = ext - mid;

      // Discard optional hyphen, underscore or space between name and midpart
      if (midlen > 0 &&
          (mid [0] == '-' || mid [0] == '_' || mid [0] == ' '))
      {
         ++mid;
         --midlen;
      }

      // Apply some simple heuristics to identify font variants
      if (midlen == 0 ||
          strncasecmp (mid, "R", midlen) == 0 ||
          strncasecmp (mid, "Regular", midlen) == 0)
      {
         if (saveCandidate (fpath, ext, ftwbuf->level,
                            "Regular", sstate.regular))
            return 0;
      }
      else if (strncasecmp (mid, "B", midlen) == 0 ||
               strncasecmp (mid, "Bold", midlen) == 0)
      {
         if (saveCandidate (fpath, ext, ftwbuf->level,
                            "Bold", sstate.bold))
            return 0;
      }
      else if (strncasecmp (mid, "I", midlen) == 0 ||
               strncasecmp (mid, "It", midlen) == 0 ||
               strncasecmp (mid, "Italic", midlen) == 0 ||
               strncasecmp (mid, "O", midlen) == 0 ||
               strncasecmp (mid, "Ob", midlen) == 0 ||
               strncasecmp (mid, "Oblique", midlen) == 0)
      {
         if (saveCandidate (fpath, ext, ftwbuf->level,
                            "Italic", sstate.italic))
            return 0;
      }
      else if (strncasecmp (mid, "BI", midlen) == 0 ||
               strncasecmp (mid, "BoldIt", midlen) == 0 ||
               strncasecmp (mid, "BoldItalic", midlen) == 0)
      {
         if (saveCandidate (fpath, ext, ftwbuf->level,
                            "BoldItalic", sstate.boldItalic))
            return 0;
      }

   #if 0
      // TODO remove the below printouts along with the include of stdio.h
      printf("%-3s %2d ",
             (tflag == FTW_D) ?   "d"   : (tflag == FTW_DNR) ? "dnr" :
             (tflag == FTW_DP) ?  "dp"  : (tflag == FTW_F) ?   "f" :
             (tflag == FTW_NS) ?  "ns"  : (tflag == FTW_SL) ?  "sl" :
             (tflag == FTW_SLN) ? "sln" : "???",
             ftwbuf->level);

      if (tflag == FTW_NS)
         printf("-------");
      else
         printf("%7jd", (intmax_t) sb->st_size);

      printf("   %-40s %d %s\n",
             fpath, ftwbuf->base, fpath + ftwbuf->base);
   #endif
      return 0;
   }
}

namespace zutty {

   Fontpack::Fontpack (const std::string& fontpath,
                       const std::string& fontname)
   {
      logT << "Fontpack: fontpath=" << fontpath
           << "; fontname=" << fontname << std::endl;

      sstate.fontname = fontname.data ();
      sstate.fontnamelen = fontname.size ();

      int flags = FTW_DEPTH;
      if (nftw (fontpath.c_str (), fontFileFilter, 16, flags) == -1)
      {
         SYS_ERROR ("Cannot walk file tree; nftw()");
      }

      if (! sstate.regular.size ())
      {
         logE << "No Regular variant of the requested font '" << fontname
              << "' could be identified.";
         throw std::runtime_error (std::string ("No suitable files for '") +
                                   fontname + "' found!");
      }

      fontRegular = std::make_unique <Font> (sstate.regular);

      if (sstate.bold.size ())
         fontBold =
            std::make_unique <Font> (sstate.bold, * fontRegular.get ());

      if (sstate.italic.size ())
         fontItalic =
            std::make_unique <Font> (sstate.italic, * fontRegular.get ());

      if (sstate.boldItalic.size ())
         fontBoldItalic =
            std::make_unique <Font> (sstate.boldItalic, * fontRegular.get ());
   }

}
