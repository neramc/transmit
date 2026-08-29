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
constexpr std::uint32_t kMove = 10;
constexpr std::uint32_t kCarriesData = 11;
}  // namespace entry_field

namespace pair_field {
constexpr std::uint32_t kKey = 1;
constexpr std::uint32_t kValue = 2;
}  // namespace pair_field

namespace state_field {
constexpr std::uint32_t kRole = 1;
constexpr std::uint32_t kOs = 2;  ///< repeated pair records, one per candidate
constexpr std::uint32_t kExclude = 3;
constexpr std::uint32_t kId = 4;
constexpr std::uint32_t kContent = 5;
}  // namespace state_field

namespace content_field {
constexpr std::uint32_t kPath = 1;
constexpr std::uint32_t kRole = 2;
constexpr std::uint32_t kFormat = 3;
constexpr std::uint32_t kPortable = 4;
constexpr std::uint32_t kSensitive = 5;
constexpr std::uint32_t kLive = 6;
constexpr std::uint32_t kNote = 7;
constexpr std::uint32_t kChild = 8;
}  // namespace content_field

namespace move_field {
constexpr std::uint32_t kFromOs = 1;
constexpr std::uint32_t kToOs = 2;
constexpr std::uint32_t kRoot = 3;
constexpr std::uint32_t kFile = 4;
constexpr std::uint32_t kFormat = 5;
constexpr std::uint32_t kAction = 6;
constexpr std::uint32_t kKey = 7;
constexpr std::uint32_t kTarget = 8;
constexpr std::uint32_t kNote = 9;
constexpr std::uint32_t kRewrite = 10;
constexpr std::uint32_t kAssignment = 11;
}  // namespace move_field

namespace rewrite_field {
constexpr std::uint32_t kFile = 1;
constexpr std::uint32_t kFormat = 2;
constexpr std::uint32_t kKey = 3;
constexpr std::uint32_t kPattern = 4;
constexpr std::uint32_t kGroup = 5;
constexpr std::uint32_t kTable = 6;
constexpr std::uint32_t kColumn = 7;
}  // namespace rewrite_field

void writeRewriteRule(ByteWriter& writer, std::uint32_t field, const RecipeRewriteRule& rule) {
    writer.putRecord(field, [&](ByteWriter& nested) {
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

void writeContent(ByteWriter& writer, std::uint32_t field, const RecipeContent& content) {
    writer.putRecord(field, [&](ByteWriter& nested) {
        nested.putString(content_field::kPath, toUtf8(content.path));
        nested.putString(content_field::kRole, toUtf8(contentRoleName(content.role)));
        if (!content.format.isEmpty()) {
            nested.putString(content_field::kFormat, toUtf8(content.format));
        }
        nested.putString(content_field::kPortable, toUtf8(portabilityName(content.portable)));
        nested.putUInt(content_field::kSensitive, content.sensitive ? 1u : 0u);
        nested.putUInt(content_field::kLive, content.live ? 1u : 0u);
        if (!content.note.isEmpty()) {
            nested.putString(content_field::kNote, toUtf8(content.note));
        }
        for (const RecipeContent& child : content.children) {
            writeContent(nested, content_field::kChild, child);
        }
    });
}

void writePair(ByteWriter& writer, std::uint32_t field, const QString& key, const QString& value) {
    writer.putRecord(field, [&](ByteWriter& nested) {
        nested.putString(pair_field::kKey, toUtf8(key));
        nested.putString(pair_field::kValue, toUtf8(value));
    });
}

format::Result<QPair<QString, QString>> readPair(format::ByteView data);

RecipeRewriteRule readRewriteRule(format::ByteView data) {
    RecipeRewriteRule rule;
    ByteReader reader(data);
    while (!reader.atEnd()) {
        const auto tag = reader.getTag();
        if (!tag)
            break;
        switch (tag->field) {
            case rewrite_field::kFile:
                if (const auto v = reader.getString())
                    rule.filePattern = fromUtf8(*v);
                break;
            case rewrite_field::kFormat:
                if (const auto v = reader.getString())
                    rule.format = fromUtf8(*v);
                break;
            case rewrite_field::kKey:
                if (const auto v = reader.getString())
                    rule.keys << fromUtf8(*v);
                break;
            case rewrite_field::kPattern:
                if (const auto v = reader.getString())
                    rule.pattern = fromUtf8(*v);
                break;
            case rewrite_field::kGroup:
                if (const auto v = reader.getVarint())
                    rule.captureGroup = static_cast<int>(*v);
                break;
            case rewrite_field::kTable:
                if (const auto v = reader.getString())
                    rule.table = fromUtf8(*v);
                break;
            case rewrite_field::kColumn:
                if (const auto v = reader.getString())
                    rule.column = fromUtf8(*v);
                break;
            default:
                if (!reader.skip(tag->type))
                    return rule;
                break;
        }
    }
    return rule;
}

RecipeContent readContent(format::ByteView data) {
    RecipeContent content;
    ByteReader reader(data);
    while (!reader.atEnd()) {
        const auto tag = reader.getTag();
        if (!tag)
            break;
        switch (tag->field) {
            case content_field::kPath:
                if (const auto v = reader.getString())
                    content.path = fromUtf8(*v);
                break;
            case content_field::kRole:
                if (const auto v = reader.getString())
                    content.role = contentRoleFromName(fromUtf8(*v));
                break;
            case content_field::kFormat:
                if (const auto v = reader.getString())
                    content.format = fromUtf8(*v);
                break;
            case content_field::kPortable:
                if (const auto v = reader.getString())
                    content.portable = portabilityFromName(fromUtf8(*v));
                break;
            case content_field::kSensitive:
                if (const auto v = reader.getVarint())
                    content.sensitive = *v != 0;
                break;
            case content_field::kLive:
                if (const auto v = reader.getVarint())
                    content.live = *v != 0;
                break;
            case content_field::kNote:
                if (const auto v = reader.getString())
                    content.note = fromUtf8(*v);
                break;
            case content_field::kChild:
                if (const auto nested = reader.getBytes())
                    content.children.push_back(readContent(*nested));
                break;
            default:
                if (!reader.skip(tag->type))
                    return content;
                break;
        }
    }
    return content;
}

RecipeMoveStep readMoveStep(format::ByteView data) {
    RecipeMoveStep step;
    ByteReader reader(data);
    while (!reader.atEnd()) {
        const auto tag = reader.getTag();
        if (!tag)
            break;
        switch (tag->field) {
            case move_field::kFromOs:
                if (const auto v = reader.getString())
                    step.fromOs = fromUtf8(*v);
                break;
            case move_field::kToOs:
                if (const auto v = reader.getString())
                    step.toOs = fromUtf8(*v);
                break;
            case move_field::kRoot:
                if (const auto v = reader.getString())
                    step.rootId = fromUtf8(*v);
                break;
            case move_field::kFile:
                if (const auto v = reader.getString())
                    step.file = fromUtf8(*v);
                break;
            case move_field::kFormat:
                if (const auto v = reader.getString())
                    step.format = fromUtf8(*v);
                break;
            case move_field::kAction:
                if (const auto v = reader.getString())
                    step.action = moveActionFromName(fromUtf8(*v));
                break;
            case move_field::kKey:
                if (const auto v = reader.getString())
                    step.keys << fromUtf8(*v);
                break;
            case move_field::kTarget:
                if (const auto v = reader.getString())
                    step.target = fromUtf8(*v);
                break;
            case move_field::kNote:
                if (const auto v = reader.getString())
                    step.note = fromUtf8(*v);
                break;
            case move_field::kRewrite:
                if (const auto nested = reader.getBytes())
                    step.rewrites.push_back(readRewriteRule(*nested));
                break;
            case move_field::kAssignment:
                if (const auto nested = reader.getBytes()) {
                    if (const auto pair = readPair(*nested)) {
                        step.assignments.push_back(
                            RecipeMoveStep::Assignment{pair->first, pair->second});
                    }
                }
                break;
            default:
                if (!reader.skip(tag->type))
                    return step;
                break;
        }
    }
    return step;
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
    recipe.moves = moves;
    recipe.portability.carriesData = carriesData;
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
                    nested.putString(state_field::kId, toUtf8(state.id));

                    // One record per candidate rather than per system, so the
                    // order the catalog gave - which is the order they are
                    // tried in - survives the journey.
                    for (auto it = state.candidatesByOs.constBegin();
                         it != state.candidatesByOs.constEnd(); ++it) {
                        for (const QString& candidate : it.value()) {
                            writePair(nested, state_field::kOs, it.key(), candidate);
                        }
                    }
                    for (const QString& pattern : state.excludePatterns) {
                        nested.putString(state_field::kExclude, toUtf8(pattern));
                    }
                    for (const RecipeContent& content : state.contents) {
                        writeContent(nested, state_field::kContent, content);
                    }
                });
            }

            for (const RecipeRewriteRule& rule : recipe.rewrites) {
                writeRewriteRule(entry, entry_field::kRewrite, rule);
            }

            for (const RecipeMoveStep& step : recipe.moves) {
                entry.putRecord(entry_field::kMove, [&](ByteWriter& nested) {
                    nested.putString(move_field::kFromOs, toUtf8(step.fromOs));
                    nested.putString(move_field::kToOs, toUtf8(step.toOs));
                    nested.putString(move_field::kRoot, toUtf8(step.rootId));
                    nested.putString(move_field::kFile, toUtf8(step.file));
                    if (!step.format.isEmpty()) {
                        nested.putString(move_field::kFormat, toUtf8(step.format));
                    }
                    nested.putString(move_field::kAction, toUtf8(moveActionName(step.action)));
                    for (const QString& key : step.keys) {
                        nested.putString(move_field::kKey, toUtf8(key));
                    }
                    if (!step.target.isEmpty()) {
                        nested.putString(move_field::kTarget, toUtf8(step.target));
                    }
                    if (!step.note.isEmpty()) {
                        nested.putString(move_field::kNote, toUtf8(step.note));
                    }
                    for (const RecipeRewriteRule& rule : step.rewrites) {
                        writeRewriteRule(nested, move_field::kRewrite, rule);
                    }
                    for (const RecipeMoveStep::Assignment& assignment : step.assignments) {
                        writePair(nested, move_field::kAssignment, assignment.key,
                                  assignment.value);
                    }
                });
            }

            entry.putUInt(entry_field::kCarriesData, recipe.portability.carriesData ? 1u : 0u);
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
                        } else if (stateTag->field == state_field::kId) {
                            if (const auto v = stateReader.getString())
                                state.id = fromUtf8(*v);
                        } else if (stateTag->field == state_field::kOs) {
                            if (const auto nested = stateReader.getBytes()) {
                                if (const auto pair = readPair(*nested)) {
                                    state.candidatesByOs[pair->first] << pair->second;
                                }
                            }
                        } else if (stateTag->field == state_field::kContent) {
                            if (const auto nested = stateReader.getBytes())
                                state.contents.push_back(readContent(*nested));
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
                    entry.rewrites.push_back(readRewriteRule(*bytes));
                    break;
                }
                case entry_field::kMove: {
                    const auto bytes = entryReader.getBytes();
                    if (!bytes) {
                        damaged = true;
                        break;
                    }
                    entry.moves.push_back(readMoveStep(*bytes));
                    break;
                }
                case entry_field::kCarriesData:
                    if (const auto v = entryReader.getVarint())
                        entry.carriesData = *v != 0;
                    break;
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
