# gas-consumption-chart

## Purpose

Веб-график расхода газа на вкладке «Виртуальный счётчик газа»: выбор периода, итог, шкала и всплывающая подсказка.

## Requirements

### Requirement: Period selector

The gas consumption chart SHALL provide a ComboBox (dropdown) with exactly six fixed options, in order: «Сегодня (по часам)», «Вчера (по часам)», «Последние 7 дней (по дням)», «Последние 30 дней (по дням)», «В этом месяце (по дням)», «Последний месяц (по дням)». No manual date or time picker SHALL be provided.

#### Scenario: User switches period

- **WHEN** the user selects a different option in the ComboBox
- **THEN** the chart re-renders using the data for the selected period without a page reload

### Requirement: Period data resolution

The chart SHALL resolve each ComboBox option to a data range as follows:

- «Сегодня» — hourly buckets of the current local day (0..23)
- «Вчера» — hourly buckets of the previous local day (0..23)
- «Последние 7 дней» — the most recent 7 daily buckets ending today (today shown as running)
- «Последние 30 дней» — the most recent 30 daily buckets ending today (today shown as running)
- «В этом месяце» — daily buckets from the 1st of the current local month through today
- «Последний месяц» — daily buckets of the previous local calendar month

#### Scenario: Hourly periods use hourly buckets

- **WHEN** the user selects «Сегодня (по часам)» or «Вчера (по часам)»
- **THEN** the chart shows up to 24 bars, one per hour, with hour labels in HH:00 format (00:00..23:00) and zero for hours without data

#### Scenario: Daily periods use daily buckets

- **WHEN** the user selects any of the four daily periods
- **THEN** the chart shows one bar per day with day labels and a running (partial) bar for today where applicable

### Requirement: Period total and hover balloon

The chart SHALL display a total (sum of m³) for the selected period. Each bar SHALL show a styled popup balloon on hover with its label and m³ value. The running bucket (today) SHALL be visually marked.

#### Scenario: Total reflects the selected period

- **WHEN** the user selects any period
- **THEN** the header shows the sum of the displayed buckets in m³

#### Scenario: Hovering a bar shows a balloon

- **WHEN** the user hovers the pointer over a bar
- **THEN** a popup balloon appears near the top of the chart showing the bucket label and its m³ value, and it hides when the pointer leaves the bar

### Requirement: Comparability scale

The chart SHALL render a Y-axis scale with horizontal gridlines and numeric m³ labels so that bar sizes can be compared visually. Bar heights SHALL be proportional to the scale maximum (rounded up to a "nice" value).

#### Scenario: Scale is shown

- **WHEN** the chart renders a non-empty period
- **THEN** horizontal gridlines with m³ labels are drawn behind the bars

### Requirement: Empty state

When there is no data for the selected period, the chart SHALL show a placeholder message instead of an empty chart area.

#### Scenario: No data yet

- **WHEN** the selected period has no accumulated data (e.g. no wall-clock sync yet)
- **THEN** the chart area shows the placeholder message and hides the empty chart
