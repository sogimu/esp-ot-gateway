## MODIFIED Requirements

### Requirement: Period total and hover balloon

The chart SHALL display the selected period's total split into three absolute values: heating (m³), DHW (m³), and combined total (m³). Each bar SHALL show a styled popup balloon on hover with its label and the same three absolute values. The running bucket (today) SHALL be visually marked by full opacity.

#### Scenario: Total reflects the selected period

- **WHEN** the user selects any period
- **THEN** the header shows the sum of the displayed buckets split into heating, DHW, and combined total, each in m³

#### Scenario: Hovering a bar shows a balloon

- **WHEN** the user hovers the pointer over a bar
- **THEN** a popup balloon appears near the top of the chart showing the bucket label and its heating, DHW, and total m³ values, and it hides when the pointer leaves the bar

## ADDED Requirements

### Requirement: DHW/heating color coding and legend

The chart SHALL render each bar as two stacked color segments — heating (bottom, green `#a5d6a7`) and DHW (top, orange `#ff7043`) — with heights proportional to their shares of the bucket. The running bucket (today) SHALL be marked by full opacity (1.0) versus 0.7 for past buckets, without a distinct color. A legend SHALL explain the two colors; it SHALL be placed to the right of the chart and vertically centered relative to the chart area. The bar hover balloon SHALL use the same color notation for the heating and DHW values.

#### Scenario: Bar is split into heating and DHW segments

- **WHEN** the chart renders a non-empty period
- **THEN** each bar shows a green heating segment and an orange DHW segment whose heights are proportional to `m3_total − m3_dhw` and `m3_dhw` respectively

#### Scenario: Legend explains the colors

- **WHEN** the chart renders
- **THEN** a legend labels the green color as heating and the orange color as DHW, positioned to the right of the chart and vertically centered relative to it

#### Scenario: Balloon uses the legend colors

- **WHEN** the user hovers a bar
- **THEN** the balloon marks the DHW value with the orange legend color and the heating value with the green legend color

#### Scenario: Today is marked by opacity only

- **WHEN** the chart renders a running (today) bucket
- **THEN** the bucket uses full opacity (1.0) while past buckets use 0.7, without a distinct color
