# Desktop Application Design System

> A design and UX specification for building a polished, modern, high-performance cross-platform desktop application for Windows, Linux, and macOS.
>
> This document is intended to be used as a source of truth for AI-assisted / vibe coding workflows.

---

## 1. Design Vision

The application should feel like a **professional native desktop application**, not a website wrapped inside a desktop window.

The design philosophy is:

> **Modern Native Desktop UI + Consistent Product Identity + OS-Adaptive UX**

The application must maintain one coherent visual identity across Windows, Linux, and macOS while adapting platform-specific behavior where users expect it.

### Primary design characteristics

- Modern
- Minimal
- Professional
- Fast
- Information-dense
- Calm
- Accessible
- Keyboard-friendly
- Native-feeling
- Consistent
- Avoid unnecessary decoration

The interface should communicate:

> "This is a serious desktop application designed specifically for desktop users."

It must not feel like:

- A mobile application enlarged to desktop size
- A generic web dashboard
- A futuristic concept UI
- A glassmorphism showcase
- A UI overloaded with gradients, shadows, or animations

---

# 2. Core Principles

## 2.1 Native First

Prefer platform conventions over forcing a completely platform-independent UI.

The application's:

- layout
- colors
- typography
- iconography
- spacing
- component behavior

should remain consistent.

However, these may adapt to the operating system:

- window controls
- keyboard shortcuts
- application menus
- context menus
- dialogs
- system notifications
- file pickers
- title bar behavior
- drag and drop behavior

### Rule

**Keep the visual language consistent. Adapt the interaction model when platform conventions matter.**

---

# 3. Platform Design Direction

Use the following design influences:

| Source | Influence |
|---|---:|
| Fluent 2 | 40% |
| GNOME / Adwaita | 30% |
| macOS Human Interface Guidelines | 30% |

This is not a requirement to copy these design systems.

Instead, extract their strengths:

### Fluent

Use for:

- modern controls
- spacing
- hierarchy
- subtle depth
- desktop layout patterns
- interaction states

### GNOME / Adwaita

Use for:

- simplicity
- restrained visual hierarchy
- accessibility
- clean settings layouts
- predictable controls

### macOS

Use for:

- native interaction conventions
- dialogs
- menus
- keyboard behavior
- window behavior
- platform-specific polish

---

# 4. Overall UI Structure

The default application structure should be:

```text
┌─────────────────────────────────────────────────────────────┐
│                    Native Window Area                      │
├───────────────┬─────────────────────────────────────────────┤
│               │                                             │
│    Sidebar    │              Main Content                  │
│               │                                             │
│    240px      │                                             │
│               │                                             │
│               │                                             │
│               │                                             │
│               │                                             │
│               │                                             │
└───────────────┴─────────────────────────────────────────────┘
```

### Sidebar

Default width:

```text
Expanded: 240px
Compact:   64px
```

The sidebar should support:

- navigation
- active route indicator
- icons
- optional badges
- section groups
- user/application area
- settings entry

The sidebar should be collapsible.

Do not permanently consume excessive horizontal space.

---

# 5. Layout System

Use an **8px spacing system**.

Recommended spacing values:

```text
4px   → micro spacing
8px   → tight spacing
12px  → compact spacing
16px  → standard spacing
20px  → comfortable spacing
24px  → section spacing
32px  → large spacing
40px  → major section spacing
48px  → page-level spacing
64px  → exceptional spacing
```

### General rule

Prefer:

```text
8 / 12 / 16 / 24 / 32
```

instead of arbitrary values.

Avoid excessive whitespace.

Desktop users should be able to see meaningful information without scrolling unnecessarily.

---

# 6. Content Width

Main content should have a readable maximum width when displaying text-heavy content.

Recommended:

```text
Standard content:
max-width: 1200px

Text-heavy content:
max-width: 800px

Wide data interfaces:
max-width: none
```

Use responsive behavior when resizing the window.

The application must remain useful at:

```text
1280×720
1366×768
1440×900
1920×1080
2560×1440
3840×2160
```

Do not design exclusively around one screen resolution.

---

# 7. Typography

Prefer system-native fonts.

### Recommended font strategy

```text
Windows:
Segoe UI Variable / Segoe UI

macOS:
SF Pro / system-ui

Linux:
system-ui / Inter / Noto Sans
```

If the framework supports system font stacks, prefer them.

### Typography hierarchy

```text
Display        28–36px
Page Title     24–30px
Section Title  18–22px
Body           14–16px
Secondary      13–14px
Caption        12px
```

### Weight

Use:

```text
Regular
Medium
Semibold
Bold
```

Do not use heavy font weights everywhere.

Typography should create hierarchy through:

- size
- weight
- spacing
- contrast

rather than excessive color.

---

# 8. Color System

Use a restrained color palette.

The application should have:

```text
Primary
Background
Surface
Elevated Surface
Border
Text
Secondary Text
Disabled Text
Success
Warning
Error
Information
```

### Light theme example

```text
Background       #FFFFFF
Surface          #F8F8FA
Elevated         #FFFFFF
Border           #E5E7EB
Text             #18181B
Secondary Text   #71717A
Disabled Text    #A1A1AA
```

### Dark theme example

```text
Background       #09090B
Surface          #18181B
Elevated         #202023
Border           #27272A
Text             #FAFAFA
Secondary Text   #A1A1AA
Disabled Text    #52525B
```

These exact values are examples, not immutable requirements.

The design system must allow the colors to be centralized as tokens.

---

# 9. Accent Color

Use one primary brand/accent color.

The accent should be used for:

- primary actions
- active navigation
- focus indicators
- selected controls
- links
- progress
- important highlights

Do not make every element colorful.

### Rule

> Accent color communicates meaning. It is not decoration.

Semantic colors should be distinct from the brand color:

```text
Success → positive state
Warning → caution
Error   → destructive/error state
Info    → informational state
```

Never communicate important information using color alone.

---

# 10. Dark Mode

The application must support:

```text
Light
Dark
System
```

The default should normally be:

```text
System
```

unless the product has a specific reason to choose otherwise.

Dark mode must be designed independently rather than generated by simply inverting light-mode colors.

Ensure:

- sufficient contrast
- readable borders
- readable disabled states
- distinguishable surfaces
- accessible focus states
- readable icons

Avoid pure black backgrounds unless appropriate.

---

# 11. Border Radius

Use moderate corner radii.

Recommended:

```text
Small controls:     6–8px
Inputs:             6–8px
Cards:              10–12px
Dialogs:            12–16px
Large containers:   12–16px
```

Avoid excessive pill-shaped components.

Do not use:

```text
20px+ radius
```

for every component.

Pills should be reserved for:

- tags
- compact status indicators
- special controls
- intentional visual accents

---

# 12. Borders and Shadows

Prefer borders over heavy shadows.

### Default approach

```text
Border + Surface contrast
```

instead of:

```text
Large shadow + floating card
```

Shadows should communicate elevation only when necessary.

Avoid dramatic shadows.

The interface should feel crisp and lightweight.

---

# 13. Buttons

Primary button:

```text
[ Save ]
```

Secondary button:

```text
[ Cancel ]
```

Tertiary / ghost button:

```text
[ More ]
```

Danger:

```text
[ Delete ]
```

### Button hierarchy

Only one visually dominant primary action should normally exist within a local context.

Do not make every button primary.

### States

Every interactive component must support:

```text
Default
Hover
Pressed
Focused
Disabled
Loading
```

---

# 14. Inputs

Inputs should be visually simple.

Example:

```text
┌────────────────────────────────────┐
│ Enter project name                 │
└────────────────────────────────────┘
```

Recommended:

```text
Height:
32–40px

Radius:
6–8px
```

Do not create extremely tall form controls.

Input focus must be clearly visible.

Validation should communicate:

```text
Normal
Warning
Error
Success
```

Do not rely solely on a border color.

---

# 15. Navigation

Primary navigation should generally use a sidebar.

Example:

```text
┌────────────────────┐
│ APP                │
│                    │
│ 🏠 Home            │
│ 📁 Projects        │
│ 🔍 Search          │
│ 📊 Activity        │
│                    │
│ ─────────────────  │
│                    │
│ ⚙ Settings         │
└────────────────────┘
```

### Active item

The current navigation item should be immediately recognizable.

Use:

- subtle background
- accent indicator
- icon emphasis
- text weight

Do not depend only on color.

---

# 16. Command Palette

A command palette should be provided for applications with multiple actions or navigation destinations.

Recommended shortcut:

```text
Windows/Linux:
Ctrl + K

macOS:
Cmd + K
```

Example:

```text
┌──────────────────────────────────────┐
│ 🔍  Search commands...               │
├──────────────────────────────────────┤
│   New Project                        │
│   Open Project                       │
│   Settings                           │
│   Toggle Sidebar                     │
│   Check for Updates                  │
└──────────────────────────────────────┘
```

The command palette should support:

- fuzzy search
- keyboard navigation
- categories
- shortcuts
- recently used commands
- contextual actions

---

# 17. Keyboard-First UX

Desktop applications should be fully usable without a mouse whenever practical.

Recommended shortcuts:

```text
Ctrl/Cmd + K  → Command Palette
Ctrl/Cmd + P  → Quick Open
Ctrl/Cmd + F  → Search
Ctrl/Cmd + ,  → Settings
Ctrl/Cmd + N  → New
Esc           → Cancel / Close
```

Adapt modifier keys to the platform.

Never hard-code Windows shortcuts onto macOS.

---

# 18. Context Menus

Use context menus for secondary actions.

Example:

```text
┌─────────────────────┐
│ Open                │
│ Rename              │
│ Duplicate           │
│ ─────────────────── │
│ Delete              │
└─────────────────────┘
```

Keep context menus:

- compact
- predictable
- keyboard accessible
- platform appropriate

Destructive actions should be visually differentiated.

---

# 19. Dialogs

Dialogs should be used only when an action requires focused attention.

Good examples:

- destructive confirmations
- configuration that cannot fit inline
- authentication
- critical errors
- important choices

Avoid using dialogs for ordinary navigation.

### Dialog structure

```text
┌────────────────────────────────────┐
│ Delete Project                     │
│                                    │
│ This action cannot be undone.      │
│                                    │
│                 [ Cancel ] [ Delete ]│
└────────────────────────────────────┘
```

The destructive action must be explicit.

---

# 20. Toasts and Notifications

Use toast notifications for lightweight feedback.

Examples:

```text
✓ Project saved

✓ File copied

⚠ Connection unstable

✕ Failed to save changes
```

Toasts should:

- be short
- explain what happened
- optionally provide one action
- not block the workflow

Do not use toasts for information that users must read carefully.

---

# 21. Loading States

Never leave the user wondering whether something is happening.

Every asynchronous operation should have an appropriate state.

Use:

```text
Progress bar
Spinner
Skeleton
Loading text
Disabled action state
```

Prefer skeletons for large content areas.

Avoid loading spinners everywhere.

---

# 22. Empty States

Empty states should explain what the user should do next.

Bad:

```text
No data.
```

Good:

```text
No projects yet.

Create your first project to get started.

[ Create Project ]
```

An empty state should answer:

1. What happened?
2. Why is it empty?
3. What can I do next?

---

# 23. Error States

Errors should be understandable.

Never expose raw technical errors as the primary message.

Bad:

```text
ERR_NETWORK_CHANGED
```

Better:

```text
Unable to connect

The server could not be reached.
Check your connection and try again.

[ Retry ]
```

Technical details may be available through:

```text
Details
Copy error
View logs
```

---

# 24. Tables and Data-Heavy Interfaces

Desktop applications are well suited to dense data.

Use:

- compact row heights
- clear column headers
- sorting
- filtering
- search
- keyboard navigation
- column resizing when appropriate

Recommended row heights:

```text
Compact:     32px
Standard:    36–40px
Comfortable: 44–48px
```

Avoid oversized table rows.

---

# 25. Cards

Cards should group related content.

Cards should not be used for everything.

Good uses:

- dashboards
- grouped metrics
- independent settings sections
- summaries

Avoid:

```text
Card inside card inside card
```

Prefer flatter layouts when possible.

---

# 26. Icons

Use one coherent icon family.

Recommended sizes:

```text
16px → compact UI
18px → secondary controls
20px → standard UI
24px → navigation / prominent controls
32px → large visual elements
```

Icons must not randomly vary in visual weight.

Avoid mixing several unrelated icon styles.

Icons should supplement text, not replace essential labels unless the icon is universally understood.

---

# 27. Tooltips

Tooltips are useful for:

- unfamiliar icons
- truncated labels
- advanced controls

Avoid tooltips for obvious buttons.

Tooltip content should be:

```text
short
specific
useful
```

Example:

```text
Toggle Sidebar
```

not:

```text
This button can be used to toggle the visibility of the sidebar.
```

---

# 28. Animation and Motion

Motion should be subtle.

Recommended durations:

```text
Instant feedback: 80–120ms
Small transitions: 100–150ms
Panels:           150–200ms
Dialogs:          150–200ms
Page transitions: 200–250ms
```

Avoid long animations.

Do not animate:

- every element
- simple state changes unnecessarily
- large amounts of content without purpose

### Principle

> Animation should clarify state, hierarchy, or spatial relationships.

It should never exist merely to look impressive.

---

# 29. Performance-First Design

The application is a desktop application, so responsiveness is a feature.

Prioritize:

```text
Fast startup
Low memory usage
Responsive interactions
Smooth scrolling
Low input latency
Efficient rendering
Lazy loading when appropriate
```

Avoid visual effects that have measurable performance costs without meaningful UX benefits.

Be especially careful with:

- large blur effects
- excessive shadows
- continuously animated backgrounds
- unnecessary gradients
- unnecessary GPU effects
- rendering huge component trees
- frequent layout recalculation

The interface should remain responsive during heavy operations.

---

# 30. Native OS Integration

Where supported, use native platform behavior.

Examples:

```text
File picker
Folder picker
Clipboard
Drag & Drop
Notifications
Window management
System menus
Open with...
Reveal in file manager
System theme
System font
```

Do not recreate native functionality purely for visual consistency unless there is a strong product reason.

---

# 31. Platform Adaptation

The application should share one visual identity while respecting platform differences.

### Windows

Prioritize:

- Windows keyboard conventions
- native window behavior
- familiar context menus
- Fluent-inspired controls
- Windows-style title bar handling

### macOS

Prioritize:

- Cmd-based shortcuts
- native application menu behavior
- native window controls
- macOS dialogs where appropriate
- platform-consistent spacing

### Linux

Support common desktop environments when practical.

The application should behave naturally under:

- GNOME
- KDE Plasma
- Xfce

Do not assume that all Linux desktops behave identically.

---

# 32. Responsive Window Behavior

Desktop UI must handle resizing gracefully.

Define layout behavior for:

```text
Large window
Medium window
Small window
```

Example:

```text
Large:
Sidebar + full content

Medium:
Sidebar + reduced content density

Small:
Compact sidebar or collapsed navigation
```

Do not allow content to overlap or become unusable.

Avoid horizontal scrolling for ordinary application navigation.

---

# 33. Accessibility

Accessibility is part of the design, not an optional enhancement.

Required:

- keyboard navigation
- visible focus state
- semantic labels
- screen reader support where applicable
- sufficient color contrast
- non-color state indicators
- scalable text where practical
- accessible dialogs
- accessible form validation

Every interactive element must be reachable and understandable using the keyboard.

---

# 34. Focus Management

Keyboard focus must always be obvious.

Do not remove focus outlines without providing an accessible replacement.

Focus should move logically after:

- opening dialogs
- closing dialogs
- changing views
- deleting items
- creating items

When a modal closes, return focus to the triggering control whenever practical.

---

# 35. Information Hierarchy

Every screen should have a clear hierarchy.

Recommended:

```text
Page title
↓
Page description / context
↓
Primary action
↓
Main content
↓
Secondary actions
```

Users should immediately understand:

1. Where they are
2. What they are looking at
3. What they can do
4. What action is most important

---

# 36. Visual Density

The UI should be:

> **Information-dense, but visually calm.**

Do not use enormous whitespace like a mobile interface.

Do not compress everything until it becomes difficult to scan.

The ideal density resembles professional tools such as:

- IDEs
- productivity applications
- engineering tools
- creative applications
- system utilities

---

# 37. Design Tokens

All design values must be centralized.

Example:

```text
tokens/
├── colors
├── spacing
├── typography
├── radius
├── shadows
├── animation
├── sizing
└── breakpoints
```

Do not scatter raw values throughout components.

Prefer:

```text
spacing-md
color-surface
radius-md
text-secondary
```

instead of repeatedly writing arbitrary values.

---

# 38. Component Architecture

Build reusable primitives first.

Recommended hierarchy:

```text
Primitive
    ↓
Component
    ↓
Pattern
    ↓
Page
```

Example:

```text
Button
↓
ActionButton
↓
Toolbar
↓
ProjectPage
```

Avoid duplicating UI logic across pages.

When multiple components share the same behavior, extract the behavior into a reusable component.

---

# 39. Component State Requirements

Every reusable interactive component should consider:

```text
default
hover
pressed
focus
disabled
loading
selected
error
success
```

Not every component needs every state, but state behavior must be intentional.

Avoid ambiguous states.

---

# 40. Do Not Over-Design

The following are discouraged unless they provide clear UX value:

- excessive gradients
- excessive glassmorphism
- giant rounded cards
- giant typography
- excessive shadows
- decorative animations
- animated backgrounds
- excessive neon colors
- excessive blur
- unnecessary floating panels
- excessive pill-shaped components
- visual noise

The goal is:

> **Professional, not flashy.**

---

# 41. Anti-Patterns

Avoid creating interfaces that resemble:

### Mobile UI scaled to desktop

Problems:

- huge controls
- wasted space
- low information density

### Web dashboard inside a window

Problems:

- non-native behavior
- awkward dialogs
- wrong keyboard shortcuts
- website-like navigation

### Dribbble concept UI

Problems:

- beautiful screenshots
- poor usability
- excessive decoration
- impractical interactions

### Overloaded enterprise UI

Problems:

- too many buttons
- unclear hierarchy
- excessive tables
- intimidating first impression

---

# 42. AI Coding Rules

When generating or modifying UI, the AI must follow these rules.

## Rule 1 — Preserve the design system

Never introduce a new:

- radius
- color
- spacing value
- typography style
- icon style

without determining whether an existing token can be reused.

---

## Rule 2 — Reuse components

Before creating a new component, check whether an existing component can satisfy the requirement.

Prefer:

```text
Existing primitive + configuration
```

over:

```text
New one-off component
```

---

## Rule 3 — Do not copy web UI patterns blindly

This is a desktop application.

Do not automatically use:

- mobile bottom navigation
- mobile-style sheets everywhere
- huge hero sections
- website-style cards
- giant full-width buttons

unless the feature specifically requires them.

---

## Rule 4 — Respect the operating system

Do not force Windows behavior onto macOS or Linux.

Always consider:

```text
Keyboard modifiers
Window controls
System dialogs
Context menus
File pickers
Notifications
Native menus
```

---

## Rule 5 — Prefer functionality over decoration

When choosing between:

```text
Visual effect
```

and

```text
Usability / performance
```

choose usability and performance.

---

## Rule 6 — Keep layouts predictable

Users should be able to predict:

- where navigation lives
- where primary actions live
- where settings live
- where dialogs appear
- how forms behave
- how keyboard navigation works

---

## Rule 7 — Optimize for real desktop usage

Design for:

- mouse
- keyboard
- high-resolution displays
- resizable windows
- multiple monitors
- long sessions
- power users

---

# 43. Design Review Checklist

Before considering a UI feature complete, verify:

### Visual

- [ ] Uses the existing color tokens
- [ ] Uses the existing spacing scale
- [ ] Uses the existing radius scale
- [ ] Uses the correct typography hierarchy
- [ ] Uses one consistent icon family
- [ ] Works in light mode
- [ ] Works in dark mode

### Interaction

- [ ] Hover state exists where appropriate
- [ ] Pressed state exists where appropriate
- [ ] Focus state exists
- [ ] Disabled state exists
- [ ] Loading state exists where required
- [ ] Keyboard interaction works
- [ ] Escape closes transient UI where appropriate

### Desktop UX

- [ ] Resizes correctly
- [ ] Works at 1280×720
- [ ] Works at 1920×1080
- [ ] Does not waste excessive space
- [ ] Uses native OS conventions where appropriate
- [ ] Supports mouse and keyboard

### Accessibility

- [ ] Focus is visible
- [ ] Text is readable
- [ ] Contrast is sufficient
- [ ] Information is not conveyed only by color
- [ ] Controls have meaningful labels

### Performance

- [ ] No unnecessary animations
- [ ] No unnecessary blur effects
- [ ] No unnecessary heavy shadows
- [ ] No excessive re-rendering
- [ ] Large content is efficiently rendered
- [ ] UI remains responsive during background work

---

# 44. Priority Order

When making design decisions, follow this priority:

```text
1. Functionality
2. Usability
3. Performance
4. Accessibility
5. Platform conventions
6. Consistency
7. Visual polish
8. Decoration
```

Never sacrifice the first five merely to improve visual appearance.

---

# 45. Final Design Rule

The final application should feel like:

```text
Professional desktop software
+
Modern visual design
+
Native platform behavior
+
High information density
+
Excellent keyboard support
+
Fast and responsive interaction
+
Consistent design system
```

The target feeling is:

> **"It looks modern, but it already feels familiar."**

The best implementation is not the one with the most visual effects.

It is the one where the user can immediately understand the interface, perform tasks efficiently, and forget that the application is cross-platform.

---

# 46. AI Implementation Directive

When implementing UI based on this document:

1. Treat this file as the design source of truth.
2. Inspect existing components before creating new ones.
3. Reuse design tokens.
4. Prefer reusable primitives.
5. Maintain consistent spacing and typography.
6. Support light, dark, and system themes.
7. Respect Windows, Linux, and macOS conventions.
8. Prioritize keyboard accessibility.
9. Avoid unnecessary visual effects.
10. Optimize for responsiveness and performance.
11. Test layouts at multiple desktop resolutions.
12. Keep the interface visually calm and information-dense.
13. Do not introduce new visual patterns without a clear UX reason.
14. Prefer native platform behavior when it improves usability.
15. When uncertain, choose the simpler and more predictable solution.

**Design quality should be measured by usability, consistency, platform integration, accessibility, and performance—not by visual complexity.**