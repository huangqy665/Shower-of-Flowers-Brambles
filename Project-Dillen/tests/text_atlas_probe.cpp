#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "control_tree.hpp"
#include "overlay.hpp"
#include "presentation_compiler.hpp"
#include "presentation_schema.hpp"
#include "standalone_session.hpp"
#include "text_atlas.hpp"

// Demo 0.8 P5c -- the font, rasterised, measured and placed. No window.
//
// A wrong advance is invisible in a screenshot and obvious in an assertion,
// which is why the whole text layer is arithmetic on the CPU and why FreeType
// is NOT behind the renderer flag. The backend uploads a bitmap and draws
// quads; every decision about where a glyph goes is made here and gated here.
//
// The font is a Presentation asset with a payload and a digest, exactly like
// the index raster, and for the same reason: the payload is not an authoring
// source, the file catalog never classifies it, and the digest is the only
// thing binding the declaration to the bytes.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";
const fs::path kMapPresentationRoot = kGameRoot / "presentation/map_world";

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "text atlas: " << what << '\n';
        ++failures;
    }
}

}

int main()
{
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "world_map_contracts", kMapContractsRoot, 0, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_mechanisms", kMapMechanismRoot, 50, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_content", kMapWorldRoot, 100, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_presentation", kMapPresentationRoot, 200, {}, {}, {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.map.world_root"),
        "dillen.map.world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;

    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    if (!session.Start(config, report))
    {
        for (const std::string& diagnostic : report.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "text atlas: the world did not load\n";
        return 1;
    }

    const kernel::PresentationAsset* fontAsset = nullptr;
    for (const kernel::PresentationAsset& asset : session.PresentationAssets())
    {
        if (asset.kind == "font")
        {
            fontAsset = &asset;
            break;
        }
    }
    if (fontAsset == nullptr)
    {
        std::cerr << "text atlas: the Package declares no font\n";
        return 2;
    }

    std::string message;
    presentation::TextAtlas atlas;
    if (atlas.Load(*fontAsset, message) != presentation::TextAtlasStatus::Ok)
    {
        std::cerr << "text atlas: load failed: " << message << '\n';
        return 3;
    }

    // --- the atlas covers what the Package declared ------------------------
    Check(atlas.GlyphCount() == 95,
        "the declared codepoint range did not produce 95 glyphs");
    Check(atlas.PixelSize() == 14, "the atlas is not at the declared size");
    Check(atlas.LineHeight() > atlas.PixelSize() / 2
            && atlas.Ascender() > 0
            && atlas.Ascender() < atlas.LineHeight(),
        "the vertical metrics are not usable");
    Check(atlas.Bitmap().size()
            == static_cast<std::size_t>(atlas.Width()) * atlas.Height(),
        "the bitmap is not width x height bytes");
    // A row that is not a multiple of four bytes is read at a drifting offset
    // under GL's default unpack alignment. The backend sets the alignment too,
    // but that guard is not checkable by sampling pixels, so the width is
    // padded here where it can be asserted.
    Check(atlas.Width() % 4 == 0,
        "the atlas row is not a multiple of four bytes");

    std::size_t inked = 0;
    std::size_t empty = 0;
    for (char character = 32; character <= 126; ++character)
    {
        const presentation::GlyphMetrics* glyph = atlas.Glyph(character);
        Check(glyph != nullptr,
            std::string("no metrics for '") + character + "'");
        if (glyph == nullptr)
        {
            continue;
        }
        Check(glyph->advance > 0,
            std::string("'") + character + "' does not advance the pen");
        Check(glyph->atlasX + glyph->width <= atlas.Width()
                && glyph->atlasY + glyph->height <= atlas.Height(),
            std::string("'") + character + "' escapes the atlas");
        if (glyph->width == 0 || glyph->height == 0)
        {
            ++empty;
        }
        else
        {
            ++inked;
        }
    }
    // Space is the only glyph in this range with no coverage. Anything else
    // blank means the face was loaded but produced nothing, which is what a
    // wrong pixel size or a failed hinting setup looks like.
    Check(empty == 1 && inked == 94,
        "the only blank glyph in the range should be the space");

    // Every glyph must be rasterised somewhere in the bitmap. A packer that
    // wrote metrics but never blitted would pass everything above.
    std::size_t coveredPixels = 0;
    for (const std::uint8_t value : atlas.Bitmap())
    {
        if (value != 0)
        {
            ++coveredPixels;
        }
    }
    Check(coveredPixels > 0, "the atlas bitmap is entirely blank");
    {
        const presentation::GlyphMetrics* wide = atlas.Glyph('W');
        std::size_t insideW = 0;
        if (wide != nullptr)
        {
            for (std::int32_t row = 0; row < wide->height; ++row)
            {
                for (std::int32_t column = 0; column < wide->width; ++column)
                {
                    const std::size_t at =
                        static_cast<std::size_t>(wide->atlasY + row)
                            * atlas.Width() + wide->atlasX + column;
                    if (atlas.Bitmap()[at] != 0)
                    {
                        ++insideW;
                    }
                }
            }
        }
        Check(insideW > 0, "the box the metrics give for 'W' is blank");
    }

    // The Package ships a monospaced face, and the fixed-width formatting the
    // panel uses only reads well against one. Asserting it here is what makes
    // that a property of the content rather than a hope.
    const std::int32_t advance = atlas.Glyph('0')->advance;
    for (char character = 32; character <= 126; ++character)
    {
        Check(atlas.Glyph(character)->advance == advance,
            std::string("'") + character
                + "' does not share the monospace advance");
    }

    // --- measurement and placement ----------------------------------------
    Check(atlas.Measure("") == 0, "the empty string has width");
    Check(atlas.Measure("Level: 31") == 9 * advance,
        "measurement is not the sum of advances");
    // A codepoint outside the declared range is skipped, not substituted. A
    // box glyph would be a claim the Package cannot support.
    Check(atlas.Measure(std::string("ab\x01") + "c") == 3 * advance,
        "a codepoint outside the range was given a width");

    const presentation::ControlRect row{10, 40, 180, 20};
    const std::vector<presentation::TextQuad> quads =
        atlas.LayoutText("Level: 31", row);
    Check(quads.size() == 8,
        "the space should place no quad, leaving eight");
    for (const presentation::TextQuad& quad : quads)
    {
        Check(quad.x >= row.x && quad.x + quad.width <= row.x + row.width,
            "a glyph was placed outside its control");
        Check(quad.atlasX + quad.width <= atlas.Width()
                && quad.atlasY + quad.height <= atlas.Height(),
            "a placed quad addresses outside the atlas");
    }
    // Each quad sits at the pen for its position in the string, offset by its
    // own left bearing. Comparing consecutive quad positions would NOT say
    // this -- the difference between two quads is one advance plus the
    // difference of two bearings, which is true of a great many wrong layouts.
    {
        const std::string text = "Level: 31";
        std::size_t placed = 0;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            const presentation::GlyphMetrics* glyph = atlas.Glyph(text[index]);
            if (glyph == nullptr || glyph->width == 0 || glyph->height == 0)
            {
                continue;
            }
            Check(placed < quads.size()
                    && quads[placed].x
                        == row.x + static_cast<std::int32_t>(index) * advance
                            + glyph->bearingX,
                std::string("'") + text[index]
                    + "' is not at its pen position");
            ++placed;
        }
        Check(placed == quads.size(), "a quad was placed for no character");
    }

    // The same string in the same rect must place identically. Layout is pure
    // arithmetic; if it were not, a redraw could shift the text by a pixel.
    Check(atlas.LayoutText("Level: 31", row).size() == quads.size(),
        "the same text laid out twice gave a different number of quads");

    // --- clipping ----------------------------------------------------------
    //
    // A string wider than its control must lose whole glyphs off the right
    // edge and place nothing beyond it.
    const presentation::ControlRect narrow{10, 40, advance * 4 + 2, 20};
    const std::vector<presentation::TextQuad> clipped =
        atlas.LayoutText("MMMMMMMMMM", narrow);
    Check(!clipped.empty() && clipped.size() < 10,
        "a string wider than its control was not clipped");
    for (const presentation::TextQuad& quad : clipped)
    {
        Check(quad.x + quad.width <= narrow.x + narrow.width,
            "a clipped glyph still crosses the right edge");
    }
    Check(atlas.LayoutText("MMMM", presentation::ControlRect{0, 0, 0, 20})
            .empty(),
        "a control with no width still placed glyphs");

    // --- the panel's real strings fit --------------------------------------
    //
    // The control tree and the atlas are separate pieces; this is where they
    // have to agree. Every row the package declares must be able to show its
    // caption at the size the package declares.
    const kernel::PresentationAsset* binding = nullptr;
    for (const kernel::PresentationAsset& asset : session.PresentationAssets())
    {
        if (asset.kind == "ui_binding")
        {
            binding = &asset;
            break;
        }
    }
    // The layout now comes out of the Compiler, not out of an interpreted
    // string tree. What this probe cares about is unchanged: the strings the
    // controls end up showing have to fit the boxes the layout gives them.
    presentation::PresentationSchemaRegistry schema;
    presentation::RegisterBuiltinControls(schema);
    presentation::PresentationCompiler compiler;
    presentation::FrozenPresentationCatalog catalog;
    presentation::ControlTree tree;
    const bool compiled =
        binding != nullptr
        && compiler.Compile(
               schema,
               session.PresentationAssets(),
               session.Catalog(),
               catalog,
               message)
            == presentation::PresentationCompileStatus::Ok
        && tree.Bind(
               catalog,
               presentation::StablePresentationViewId(
                   binding->canonicalName),
               session.Catalog(),
               message)
            == presentation::ControlTreeStatus::Ok;
    if (compiled)
    {
        tree.Layout(200, 300);
        presentation::MechanismPanelReadout readout;
        readout.valid = true;
        // The slots the view compiled. A value control matches its field by
        // slot now, so a synthesised readout has to carry real ones.
        const presentation::CompiledPresentationView& view = tree.View();
        for (std::size_t index = 0; index < view.boundFieldSlots.size();
            ++index)
        {
            presentation::MechanismPanelField field;
            field.label = view.boundFieldNames[index];
            field.slot = view.boundFieldSlots[index];
            field.value = index == 0 ? 31 : 314159;
            field.isDecimal = index != 0;
            readout.fields.push_back(field);
        }
        for (const presentation::ControlDraw& draw : tree.Draw(readout))
        {
            if (draw.text.empty())
            {
                continue;
            }
            const std::string id =
                tree.Value(draw.control, presentation::builtin::kId).text;
            const std::int32_t needed = atlas.Measure(draw.text);
            Check(needed <= draw.rect.width,
                "'" + draw.text + "' does not fit control '" + id + "' ("
                    + std::to_string(needed) + " > "
                    + std::to_string(draw.rect.width) + ")");
            Check(atlas.LineHeight() <= draw.rect.height,
                "control '" + id + "' is shorter than a line of text");
        }
    }
    else
    {
        Check(false, "the ui_binding did not compile: " + message);
    }

    // --- the overlay: what a backend is actually handed --------------------
    //
    // A backend that walked the control tree and called the atlas itself would
    // make these decisions behind a GPU, where the Linux cells never run them.
    // Here they are a list of rectangles, and this is where the list is
    // checked.
    if (compiled)
    {
        tree.Layout(200, 300);
        presentation::MechanismPanelReadout readout;
        readout.valid = true;
        // The slots the view compiled. A value control matches its field by
        // slot now, so a synthesised readout has to carry real ones.
        const presentation::CompiledPresentationView& view = tree.View();
        for (std::size_t index = 0; index < view.boundFieldSlots.size();
            ++index)
        {
            presentation::MechanismPanelField field;
            field.label = view.boundFieldNames[index];
            field.slot = view.boundFieldSlots[index];
            field.value = index == 0 ? 31 : 314159;
            field.isDecimal = index != 0;
            readout.fields.push_back(field);
        }

        const std::vector<presentation::OverlayQuad> quads =
            presentation::BuildPanelOverlay(
                tree, atlas, readout, UINT32_MAX);
        Check(!quads.empty(), "the panel produced nothing to draw");

        std::size_t solid = 0;
        std::size_t glyphs = 0;
        for (const presentation::OverlayQuad& quad : quads)
        {
            Check(quad.width > 0 && quad.height > 0,
                "an overlay quad has no area");
            if (quad.textured)
            {
                ++glyphs;
                Check(quad.atlasX + quad.width <= atlas.Width()
                        && quad.atlasY + quad.height <= atlas.Height(),
                    "a glyph quad addresses outside the atlas");
            }
            else
            {
                ++solid;
            }
        }
        // The root panel and the two buttons. Nested panels do not paint:
        // stacking their alpha would darken the interface by how deeply it
        // happens to nest.
        Check(solid == 3,
            "expected three solid surfaces, got " + std::to_string(solid));
        Check(glyphs > 0, "no glyphs were placed");

        // Surfaces before text, so a backend can draw the list in order with
        // no sorting and no depth buffer. If text came first it would be
        // painted over by the panel behind it.
        std::size_t firstGlyph = quads.size();
        for (std::size_t index = 0; index < quads.size(); ++index)
        {
            if (quads[index].textured)
            {
                firstGlyph = index;
                break;
            }
        }
        for (std::size_t index = firstGlyph; index < quads.size(); ++index)
        {
            Check(quads[index].textured,
                "a solid quad is listed after the text that covers it");
        }

        // Hovering changes one button and nothing else. A hover that repainted
        // the panel would be indistinguishable from one that worked.
        const std::uint32_t raise = tree.Find("raise");
        Check(raise != UINT32_MAX, "the raise button is missing");
        if (raise != UINT32_MAX)
        {
            const std::vector<presentation::OverlayQuad> hovered =
                presentation::BuildPanelOverlay(tree, atlas, readout, raise);
            Check(hovered.size() == quads.size(),
                "hovering changed how many quads are drawn");
            std::size_t differing = 0;
            for (std::size_t index = 0;
                index < quads.size() && index < hovered.size();
                ++index)
            {
                if (hovered[index].colour != quads[index].colour)
                {
                    ++differing;
                }
            }
            Check(differing == 1,
                "hovering one button changed "
                    + std::to_string(differing) + " quads");
        }

        // A Package with no font still has an interface; it just has no
        // captions. The surfaces must survive, because a panel that vanished
        // when a font failed to load would be the worst of both answers.
        presentation::TextAtlas none;
        const std::vector<presentation::OverlayQuad> mute =
            presentation::BuildPanelOverlay(
                tree, none, readout, UINT32_MAX);
        Check(mute.size() == solid,
            "a missing font took the panel surfaces with it");
    }

    // --- UTF-8, and codepoints beyond ASCII ------------------------------
    //
    // The atlas was ASCII 32-126 addressed by a signed char, which is a limit
    // of the storage rather than of anything real: FreeType will rasterise
    // whatever the face has. Text now arrives as UTF-8 and a Package declares
    // the ranges it wants.
    {
        // Decoding first, as a pure function. A wrong decoder is the kind of
        // thing that looks fine on ASCII and mangles everything else.
        struct Case
        {
            const char* text;
            char32_t expected;
            std::size_t consumed;
        };
        const Case cases[] = {
            {"A", U'A', 1},
            {"\xC2\xA9", 0x00A9, 2},              // (c), two bytes
            {"\xE4\xB8\xAD", 0x4E2D, 3},         // U+4E2D, three bytes
            {"\xF0\x9F\x97\xBA", 0x1F5FA, 4},   // U+1F5FA, four bytes
            // Malformed input costs a replacement glyph and one byte, never a
            // loop that does not terminate.
            {"\x80", 0xFFFD, 1},
            {"\xE4\xB8", 0xFFFD, 1},              // truncated
            {"\xE4\x41\x42", 0xFFFD, 1}          // bad continuation
        };
        for (const Case& entry : cases)
        {
            const std::string text = entry.text;
            std::size_t cursor = 0;
            const char32_t got =
                presentation::TextAtlas::DecodeUtf8(text, cursor);
            Check(got == entry.expected && cursor == entry.consumed,
                "decoding '" + text + "' gave U+"
                    + std::to_string(static_cast<std::uint32_t>(got))
                    + " after " + std::to_string(cursor) + " bytes");
        }
        // Every byte is consumed, whatever the input.
        const std::string mixed = "a\xE4\xB8\xAD\x80z";
        std::size_t cursor = 0;
        std::size_t steps = 0;
        while (cursor < mixed.size() && steps < 100)
        {
            presentation::TextAtlas::DecodeUtf8(mixed, cursor);
            ++steps;
        }
        Check(cursor == mixed.size(),
            "decoding a mixed string did not consume it");
    }

    // Discontiguous ranges. A Chinese interface wants Latin punctuation and a
    // block far above it and nothing in the twenty thousand codepoints
    // between, which a single first/last cannot say.
    {
        kernel::PresentationAsset split = *fontAsset;
        split.properties["codepoints"] = "48-57,65-70";
        presentation::TextAtlas ranged;
        Check(ranged.Load(split, message) == presentation::TextAtlasStatus::Ok,
            "two ranges did not load: " + message);
        if (ranged.IsLoaded())
        {
            Check(ranged.GlyphCount() == 16,
                "two ranges gave "
                    + std::to_string(ranged.GlyphCount()) + " glyphs");
            Check(ranged.Glyph(U'0') != nullptr && ranged.Glyph(U'9') != nullptr
                    && ranged.Glyph(U'A') != nullptr
                    && ranged.Glyph(U'F') != nullptr,
                "a declared codepoint is missing");
            // The gap really is a gap, not a range quietly filled in.
            Check(ranged.Glyph(U'a') == nullptr && ranged.Glyph(U'Z') == nullptr
                    && ranged.Glyph(U' ') == nullptr,
                "a codepoint between the ranges was rasterised anyway");
            Check(ranged.Measure("09AF") == 4 * ranged.Glyph(U'0')->advance
                    && ranged.Measure("az") == 0,
                "measurement does not follow the declared ranges");
        }
    }

    // Latin-1 and beyond, from the same face: the range is no longer capped
    // at 126, and the packer has to place whatever comes.
    {
        kernel::PresentationAsset wide = *fontAsset;
        wide.properties["codepoints"] = "32-126,0xA1-0xFF";
        presentation::TextAtlas latin;
        Check(latin.Load(wide, message) == presentation::TextAtlasStatus::Ok,
            "a range above ASCII did not load: " + message);
        if (latin.IsLoaded())
        {
            Check(latin.Glyph(0x00E9) != nullptr && latin.Glyph(0x00DF)
                    != nullptr,
                "a codepoint above 126 was not rasterised");
            // Every box inside the sheet, and no two boxes overlapping. The
            // packer is now shelves rather than one row, so this is the
            // property that has to hold rather than "they are all on line 1".
            std::vector<const presentation::GlyphMetrics*> boxes;
            for (char32_t codepoint = 32; codepoint <= 0xFF; ++codepoint)
            {
                if (const presentation::GlyphMetrics* glyph =
                        latin.Glyph(codepoint))
                {
                    Check(glyph->atlasX + glyph->width <= latin.Width()
                            && glyph->atlasY + glyph->height <= latin.Height(),
                        "a glyph box escapes the atlas");
                    if (glyph->width != 0 && glyph->height != 0)
                    {
                        boxes.push_back(glyph);
                    }
                }
            }
            std::size_t overlaps = 0;
            for (std::size_t a = 0; a < boxes.size(); ++a)
            {
                for (std::size_t b = a + 1; b < boxes.size(); ++b)
                {
                    const bool disjoint =
                        boxes[a]->atlasX + boxes[a]->width <= boxes[b]->atlasX
                        || boxes[b]->atlasX + boxes[b]->width
                            <= boxes[a]->atlasX
                        || boxes[a]->atlasY + boxes[a]->height
                            <= boxes[b]->atlasY
                        || boxes[b]->atlasY + boxes[b]->height
                            <= boxes[a]->atlasY;
                    if (!disjoint)
                    {
                        ++overlaps;
                    }
                }
            }
            Check(overlaps == 0,
                std::to_string(overlaps) + " pairs of glyph boxes overlap");
            Check(latin.Width() <= 4096 && latin.Height() <= 4096,
                "the atlas is larger than one 4096 sheet");
        }
    }

    // The packer really is shelves. Forced with a narrow sheet, because the
    // Latin ranges this project ships fit in one row at 4096 -- a gate that
    // could only use the default would pass whether the packer wrapped or not.
    {
        kernel::PresentationAsset narrow = *fontAsset;
        narrow.properties["atlas_max_width"] = "256";
        presentation::TextAtlas shelved;
        Check(shelved.Load(narrow, message)
                == presentation::TextAtlasStatus::Ok,
            "a narrow sheet did not load: " + message);
        if (shelved.IsLoaded())
        {
            std::vector<const presentation::GlyphMetrics*> boxes;
            std::size_t rows = 0;
            std::uint16_t seen = 0xFFFF;
            for (char32_t codepoint = 32; codepoint <= 126; ++codepoint)
            {
                const presentation::GlyphMetrics* glyph =
                    shelved.Glyph(codepoint);
                if (glyph == nullptr || glyph->width == 0)
                {
                    continue;
                }
                Check(glyph->atlasX + glyph->width <= shelved.Width()
                        && glyph->atlasY + glyph->height <= shelved.Height(),
                    "a glyph escapes the narrow sheet");
                if (glyph->atlasY != seen)
                {
                    seen = glyph->atlasY;
                    ++rows;
                }
                boxes.push_back(glyph);
            }
            Check(shelved.Width() <= 256,
                "the narrow sheet is " + std::to_string(shelved.Width())
                    + " wide, so the packer did not wrap");
            Check(rows > 1,
                "every glyph landed on one shelf in a 256 pixel sheet");
            std::size_t overlaps = 0;
            for (std::size_t a = 0; a < boxes.size(); ++a)
            {
                for (std::size_t b = a + 1; b < boxes.size(); ++b)
                {
                    const bool disjoint =
                        boxes[a]->atlasX + boxes[a]->width <= boxes[b]->atlasX
                        || boxes[b]->atlasX + boxes[b]->width
                            <= boxes[a]->atlasX
                        || boxes[a]->atlasY + boxes[a]->height
                            <= boxes[b]->atlasY
                        || boxes[b]->atlasY + boxes[b]->height
                            <= boxes[a]->atlasY;
                    if (!disjoint)
                    {
                        ++overlaps;
                    }
                }
            }
            Check(overlaps == 0,
                "glyphs overlap once the packer has to use more than one "
                "shelf");
        }
    }

    // A range the face cannot keep is refused rather than filled with the
    // hollow .notdef box -- the substitution this layer refuses everywhere
    // else. This face has no CJK, so declaring some is the honest test.
    {
        kernel::PresentationAsset absent = *fontAsset;
        absent.properties["codepoints"] = "0x4E00-0x4E0F";
        presentation::TextAtlas refused;
        Check(refused.Load(absent, message)
                == presentation::TextAtlasStatus::FontRejected,
            "codepoints the face does not have were accepted as boxes");
    }
    {
        kernel::PresentationAsset malformed = *fontAsset;
        malformed.properties["codepoints"] = "32..126";
        presentation::TextAtlas refused;
        Check(refused.Load(malformed, message)
                == presentation::TextAtlasStatus::AssetInvalid,
            "a malformed codepoint list was accepted");
    }

    // --- what the atlas refuses --------------------------------------------
    {
        kernel::PresentationAsset bad = *fontAsset;
        bad.assetDigest =
            "0000000000000000000000000000000000000000000000000000000000000000";
        presentation::TextAtlas refused;
        Check(refused.Load(bad, message)
                == presentation::TextAtlasStatus::DigestMismatch,
            "a font whose payload does not match its digest was accepted");
    }
    {
        kernel::PresentationAsset bad = *fontAsset;
        bad.properties["pixel_size"] = "0";
        presentation::TextAtlas refused;
        Check(refused.Load(bad, message)
                == presentation::TextAtlasStatus::AssetInvalid,
            "a font at zero pixels was accepted");
    }
    // A payload that really is corrupt, and the check that the digest catches
    // it BEFORE FreeType parses it. The file is restored and the restoration
    // verified, because a probe that leaves the repository broken is worse
    // than one that does not run.
    {
        const fs::path payload =
            fs::path(fontAsset->source.physicalDirectory)
                / fontAsset->assetPath;
        std::string original;
        {
            std::ifstream stream(payload, std::ios::binary);
            original.assign(
                std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>()
            );
        }
        Check(!original.empty(), "the font payload could not be read back");
        if (!original.empty())
        {
            std::string damaged = original;
            // Inside the table directory, so the file stops being a font
            // rather than merely being a font with a different pixel in it.
            damaged[20] ^= 0xFF;
            {
                std::ofstream stream(
                    payload,
                    std::ios::binary | std::ios::trunc
                );
                stream.write(
                    damaged.data(),
                    static_cast<std::streamsize>(damaged.size())
                );
            }
            presentation::TextAtlas refused;
            const presentation::TextAtlasStatus status =
                refused.Load(*fontAsset, message);
            {
                std::ofstream stream(
                    payload,
                    std::ios::binary | std::ios::trunc
                );
                stream.write(
                    original.data(),
                    static_cast<std::streamsize>(original.size())
                );
            }
            Check(status == presentation::TextAtlasStatus::DigestMismatch,
                "a damaged payload was not refused on its digest");
            presentation::TextAtlas restored;
            Check(restored.Load(*fontAsset, message)
                    == presentation::TextAtlasStatus::Ok,
                "the payload was not restored: " + message);
        }
    }

    if (failures != 0)
    {
        std::cerr << "text atlas: " << failures << " failure(s)\n";
        return 4;
    }
    std::cout << "Text atlas: passed (" << atlas.GlyphCount()
              << " glyphs at " << atlas.PixelSize() << "px into a "
              << atlas.Width() << "x" << atlas.Height()
              << " coverage bitmap, every panel row fits)\n";
    return 0;
}
