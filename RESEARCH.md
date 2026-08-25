# Summary

This document records the search for existing documentation or implementations of the IDX2 archive format and the EL01 image format. The search covered web queries in English, Japanese and Chinese, modding forums, universal extractor databases, code hosting services, and community sites. No prior documentation, tools, or analysis were found. The specification in this repository is the first public description of both formats.

## Search queries

Queries were run for every identifying name available: EL01, IDX2, KonMenu, main.sd, and ELEMENTAL SOFT (in English and Japanese), along with generic phrases about extracting desktop accessory software. Results were unrelated noise. EL01 matches a forensic disk image format and an electric scooter product; IDX2 matches a scientific dataset library and RuneScape cache files; KonMenu appears only as an accidental substring inside unrelated JavaScript. GitHub repository and code search returned no relevant projects.

## Tools and forums

- QuickBMS: no script exists for this program or vendor.
- Game Extractor (thousands of supported games): not supported.
- MultiEx Commander: not supported.
- ZenHAX and XeNTaX-descended forums: no threads on the program, vendor, or formats.

## Community content

Japanese communities that analyze desktop mascot software have extensive coverage of other programs, but only consumer content for this one: unboxing posts, demonstration videos, and reviews. No analysis or extraction work appeared.

Chinese-language results included a redistributed theme pack on a download site and a blog post describing how to localize the desktop clock, which implies resources were edited manually at some point. The blog is now inaccessible, neither source names a tool, and no trace of such a tool survives elsewhere.

## Developer

The only developer attribution anywhere in these assets is the literal string `ELEMENTAL SOFT`: it appears in every IDX2 archive header and in archives embedded inside the companion executables (the clock and mascot programs). KonMenu.exe itself carries no vendor strings, no copyright notice, and no version information.

A Japanese software circle of the same name is well documented elsewhere. It dates back to at least the PC-98 era ([doujin software gallery](https://seesaawiki.jp/akagai_pakkuri/d/ELEMENTAL%20SOFT)), operates the website [element2.sakura.ne.jp](https://element2.sakura.ne.jp/) (an older domain, elementalsoft.jp, now serves a parked page), and publishes Windows desktop-accessory programs of exactly this genre, most recently 3D fleet-mascot programs distributed through DLsite and Booth. That this circle created the K-ON! program is a reasonable inference from the matching name and product type; no public source states it outright.

Either way, the identification does not affect the format documentation: every structure in SPEC.md is derived from the shipped binaries and verified against the shipped assets, whoever made them.

## Conclusion

Approximately fifteen years after release, across three languages and every relevant venue, no prior documentation or implementation of either format exists. SPEC.md is the first public specification of IDX2 and EL01, and the tools in this repository are the first independent implementation.
