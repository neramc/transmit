#include "core/recipe/AppInventoryPayload.h"

#include "core/utils/Conversions.h"
#include "core/utils/Logging.h"
#include "format/Serialization.h"

namespace transmit::core {
namespace {

using format::ByteReader;
using format::ByteWriter;

// Field numbers are part of the archive contract: never reuse one, only append.
namespace entry_field {
constexpr std::uint32_t kRecipeId = 1;
constexpr std::uint32_t kDisplayName = 2;
constexpr std::uint32_t kVersion = 3;
constexpr std::uint32_t kPackageSource = 4;
constexpr std::uint32_t kInstallId = 5;
constexpr std::uint32_t kState = 6;
constexpr std::uint32_t kRewrite = 7;
constexpr std::uint32_t kGrade = 8;
constexpr std::uint32_t kNote = 9;
}  // namespace entry_field

namespace pair_field {
constexpr std::uint32_t kKey = 1;
constexpr std::uint32_t kValue = 2;
}  // namespace pair_field

namespace state_field {
constexpr std::uint32_t kRole = 1;
constexpr std::uint32_t kOs = 2;  ///< repeated pair records
constexpr std::uint32_t kExclude = 3;
}  // namespace state_field

namespace rewrite_field {
constexpr std::uint32_t kFile = 1;
constexpr std::uint32_t kFormat = 2;
constexpr std::uint32_t kKey = 3;
constexpr std::uint32_t kPattern = 4;
constexpr std::uint32_t kGroup = 5;
constexpr std::uint32_t kTable = 6;
constexpr std::uint32_t kColumn = 7;
}  // namespace rewrite_field

void writePair(ByteWriter& writer, std::uint32_t field, const QString& key, const QString& value) {
    writer.putRecord(field, [&](ByteWriter& nested) {
        nested.putString(pair_field::kKey, toUtf8(key));
        nested.putString(pair_field::kValue, toUtf8(value));
    });
}

format::Result<QPair<QString, QString>> readPair(format::ByteView data) {
    QPair<QString, QString> pair;
    ByteReader reader(data);
    while (!reader.atEnd()) {
        TRANSMIT_TRY(tag, reader.getTag());
        if (tag.field == pair_field::kKey) {
            TRANSMIT_TRY(value, reader.getString());
            pair.first = fromUtf8(value);
        } else if (tag.field == pair_field::kValue) {
            TRANSMIT_TRY(value, reader.getString());
            pair.second = fromUtf8(value);
        } else {
            TRANSMIT_CHECK(reader.skip(tag.type));
        }
    }
    return pair;
}

int gradeToInt(ContinuityGrade grade) {
    return static_cast<int>(grade);
}

ContinuityGrade gradeFromInt(std::uint64_t value) {
    switch (value) {
        case 1:
            return ContinuityGrade::Adapted;
        case 2:
            return ContinuityGrade::Manual;
        case 3:
            return ContinuityGrade::Impossible;
        default:
            return ContinuityGrade::Full;
    }
}

}  // namespace

AppRecipe InventoryEntry::toRecipe() const {
    AppRecipe recipe;
    recipe.id = recipeId;
    recipe.displayName = displayName;
    recipe.installIds = installIds;
    recipe.state = state;
    recipe.rewrites = rewrites;
    recipe.expectedGrade = expectedGrade;
    recipe.note = note;
    return recipe;
}

format::ByteBuffer encodeAppInventory(const QList<MatchedApp>& matched) {
    format::ByteBuffer buffer;
    ByteWriter writer(buffer);

    for (const MatchedApp& match : matched) {
        writer.putRecord(1, [&](ByteWriter& entry) {
            const AppRecipe& recipe = match.recipe;
            entry.putString(entry_field::kRecipeId, toUtf8(recipe.id));
            entry.putString(entry_field::kDisplayName, toUtf8(recipe.displayName));
            entry.putString(entry_field::kVersion, toUtf8(match.installation.version));
            entry.putString(entry_field::kPackageSource,
                            toUtf8(platform::packageSourceName(match.installation.source)));

            for (auto it = recipe.installIds.constBegin(); it != recipe.installIds.constEnd();
                 ++it) {
                writePair(entry, entry_field::kInstallId, it.key(), it.value());
            }

            for (const RecipeStatePath& state : recipe.state) {
                entry.putRecord(entry_field::kState, [&](ByteWriter& nested) {
                    nested.putString(state_field::kRole, toUtf8(state.role));
                    for (auto it = state.byOs.constBegin(); it != state.byOs.constEnd(); ++it) {
                        writePair(nested, state_field::kOs, it.key(), it.value());
                    }
                    for (const QString& pattern : state.excludePatterns) {
                        nested.putString(state_field::kExclude, toUtf8(pattern));
                    }
                });
            }

            for (const RecipeRewriteRule& rule : recipe.rewrites) {
                entry.putRecord(entry_field::kRewrite, [&](ByteWriter& nested) {
                    nested.putString(rewrite_field::kFile, toUtf8(rule.filePattern));
                    nested.putString(rewrite_field::kFormat, toUtf8(rule.format));
                    for (const QString& key : rule.keys) {
                        nested.putString(rewrite_field::kKey, toUtf8(key));
                    }
                    if (!rule.pattern.isEmpty()) {
                        nested.putString(rewrite_field::kPattern, toUtf8(rule.pattern));
                    }
                    nested.putUInt(rewrite_field::kGroup,
                                   static_cast<std::uint64_t>(std::max(0, rule.captureGroup)));
                    if (!rule.table.isEmpty()) {
                        nested.putString(rewrite_field::kTable, toUtf8(rule.table));
                        nested.putString(rewrite_field::kColumn, toUtf8(rule.column));
                    }
                });
            }

            entry.putUInt(entry_field::kGrade,
                          static_cast<std::uint64_t>(gradeToInt(recipe.expectedGrade)));
            if (!recipe.note.isEmpty()) {
                entry.putString(entry_field::kNote, toUtf8(recipe.note));
            }
        });
    }
    return buffer;
}

QList<InventoryEntry> decodeAppInventory(format::ByteView data) {
    QList<InventoryEntry> entries;
    ByteReader reader(data);

    while (!reader.atEnd()) {
        const auto tag = reader.getTag();
        if (!tag) {
            qCWarning(logRecipe) << "the application list in this archive is damaged";
            break;
        }
        if (tag->field != 1) {
            if (!reader.skip(tag->type)) {
                break;
            }
            continue;
        }

        const auto payload = reader.getBytes();
        if (!payload) {
            break;
        }

        InventoryEntry entry;
        ByteReader entryReader(*payload);
        bool damaged = false;

        while (!entryReader.atEnd() && !damaged) {
            const auto entryTag = entryReader.getTag();
            if (!entryTag) {
                damaged = true;
                break;
            }

            switch (entryTag->field) {
                case entry_field::kRecipeId:
                    if (const auto v = entryReader.getString())
                        entry.recipeId = fromUtf8(*v);
                    else
                        damaged = true;
                    break;
                case entry_field::kDisplayName:
                    if (const auto v = entryReader.getString())
                        entry.displayName = fromUtf8(*v);
                    else
                        damaged = true;
                    break;
                case entry_field::kVersion:
                    if (const auto v = entryReader.getString())
                        entry.installedVersion = fromUtf8(*v);
                    else
                        damaged = true;
                    break;
                case entry_field::kPackageSource:
                    if (const auto v = entryReader.getString())
                        entry.packageSource = fromUtf8(*v);
                    else
                        damaged = true;
                    break;
                case entry_field::kInstallId: {
                    const auto bytes = entryReader.getBytes();
                    if (!bytes) {
                        damaged = true;
                        break;
                    }
                    if (const auto pair = readPair(*bytes)) {
                        entry.installIds.insert(pair->first, pair->second);
                    }
                    break;
                }
                case entry_field::kState: {
                    const auto bytes = entryReader.getBytes();
                    if (!bytes) {
                        damaged = true;
                        break;
                    }

                    RecipeStatePath state;
                    ByteReader stateReader(*bytes);
                    while (!stateReader.atEnd()) {
                        const auto stateTag = stateReader.getTag();
                        if (!stateTag)
                            break;
                        if (stateTag->field == state_field::kRole) {
                            if (const auto v = stateReader.getString())
                                state.role = fromUtf8(*v);
                        } else if (stateTag->field == state_field::kOs) {
                            if (const auto nested = stateReader.getBytes()) {
                                if (const auto pair = readPair(*nested)) {
                                    state.byOs.insert(pair->first, pair->second);
                                }
                            }
                        } else if (stateTag->field == state_field::kExclude) {
                            if (const auto v = stateReader.getString()) {
                                state.excludePatterns << fromUtf8(*v);
                            }
                        } else if (!stateReader.skip(stateTag->type)) {
                            break;
                        }
                    }
                    entry.state.push_back(state);
                    break;
                }
                case entry_field::kRewrite: {
                    const auto bytes = entryReader.getBytes();
                    if (!bytes) {
                        damaged = true;
                        break;
                    }

                    RecipeRewriteRule rule;
                    ByteReader ruleReader(*bytes);
                    while (!ruleReader.atEnd()) {
                        const auto ruleTag = ruleReader.getTag();
                        if (!ruleTag)
                            break;
                        switch (ruleTag->field) {
                            case rewrite_field::kFile:
                                if (const auto v = ruleReader.getString())
                                    rule.filePattern = fromUtf8(*v);
                                break;
                            case rewrite_field::kFormat:
                                if (const auto v = ruleReader.getString())
                                    rule.format = fromUtf8(*v);
                                break;
                            case rewrite_field::kKey:
                                if (const auto v = ruleReader.getString())
                                    rule.keys << fromUtf8(*v);
                                break;
                            case rewrite_field::kPattern:
                                if (const auto v = ruleReader.getString())
                                    rule.pattern = fromUtf8(*v);
                                break;
                            case rewrite_field::kGroup:
                                if (const auto v = ruleReader.getVarint())
                                    rule.captureGroup = static_cast<int>(*v);
                                break;
                            case rewrite_field::kTable:
                                if (const auto v = ruleReader.getString())
                                    rule.table = fromUtf8(*v);
                                break;
                            case rewrite_field::kColumn:
                                if (const auto v = ruleReader.getString())
                                    rule.column = fromUtf8(*v);
                                break;
                            default:
                                if (!ruleReader.skip(ruleTag->type))
                                    return entries;
                                break;
                        }
                    }
                    entry.rewrites.push_back(rule);
                    break;
                }
                case entry_field::kGrade:
                    if (const auto v = entryReader.getVarint())
                        entry.expectedGrade = gradeFromInt(*v);
                    else
                        damaged = true;
                    break;
                case entry_field::kNote:
                    if (const auto v = entryReader.getString())
                        entry.note = fromUtf8(*v);
                    else
                        damaged = true;
                    break;
                default:
                    if (!entryReader.skip(entryTag->type)) {
                        damaged = true;
                    }
                    break;
            }
        }

        // A half-parsed entry is worse than none: it would name an application
        // with a blank version and package source, and the install script
        // would then offer to reinstall something the archive never fully
        // described. `damaged` is only ever set when a value the payload said
        // was there could not be decoded - an unknown field is skipped, not
        // treated as corruption - so this drops nothing a newer writer sent.
        if (!damaged && !entry.recipeId.isEmpty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

}  // namespace transmit::core
