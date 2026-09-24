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

The chart SHALL display the selected period's total split into three absolute values: heating (m³), DHW (m³), and combined total (m³). Each bar SHALL show a styled popup balloon on hover with its label and the same three absolute values. The running bucket (today) SHALL be visually marked by full opacity.

#### Scenario: Total reflects the selected period

- **WHEN** the user selects any period
- **THEN** the header shows the sum of the displayed buckets split into heating, DHW, and combined total, each in m³

#### Scenario: Hovering a bar shows a balloon

- **WHEN** the user hovers the pointer over a bar
- **THEN** a popup balloon appears near the top of the chart showing the bucket label and its heating, DHW, and total m³ values, and it hides when the pointer leaves the bar

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

### Requirement: DHW/heating color coding and legend

The chart SHALL render each bar as two stacked color segments — heating (bottom, green `#a5d6a7`) and DHW (top, orange `#ff7043`) — with heights proportional to their shares of the bucket. The running bucket (today) SHALL be marked by full opacity (1.0) versus 0.7 for past buckets, without a distinct color. A legend SHALL explain the two colors; it SHALL be placed to the right of the chart and vertically centered relative to the chart area. The period total line and the bar hover balloon SHALL use the same color notation for the heating and DHW values.

#### Scenario: Bar is split into heating and DHW segments

- **WHEN** the chart renders a non-empty period
- **THEN** each bar shows a green heating segment and an orange DHW segment whose heights are proportional to `m3_total − m3_dhw` and `m3_dhw` respectively

#### Scenario: Legend explains the colors

- **WHEN** the chart renders
- **THEN** a legend labels the green color as heating and the orange color as DHW, positioned to the right of the chart and vertically centered relative to it

#### Scenario: Balloon uses the legend colors

- **WHEN** the user hovers a bar
- **THEN** the balloon marks the DHW value with the orange legend color and the heating value with the green legend color

#### Scenario: Period total uses the legend colors

- **WHEN** the chart renders a non-empty period
- **THEN** the period total marks the DHW sum with the orange legend color and the heating sum with the green legend color

#### Scenario: Today is marked by opacity only

- **WHEN** the chart renders a running (today) bucket
- **THEN** the bucket uses full opacity (1.0) while past buckets use 0.7, without a distinct color
