#!/usr/bin/env bash
set -euo pipefail

# ─────────────────────────────────────────────────────
# Установка Guacamole (без БД, без reverse-proxy)
# Цель: RHEL 9.4, реальный экран хоста в браузере
# Доступ: только через существующий VPN
# ─────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()  { echo -e "${GREEN}[✓]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }
die()  { echo -e "${RED}[✗]${NC} $*" >&2; exit 1; }

# ─────────────────────────────────────────────────────
# 1. Проверка прав
# ─────────────────────────────────────────────────────

if [[ $EUID -eq 0 ]]; then
    die "Не запускайте от root. Запустите от обычного пользователя с sudo."
fi

# ─────────────────────────────────────────────────────
# 2. Сбор параметров
# ─────────────────────────────────────────────────────

echo ""
echo "══════════════════════════════════════════════"
echo "  Установка Guacamole для RHEL 9.4"
echo "══════════════════════════════════════════════"
echo ""

# ─── Веб-пароль ───

while true; do
    read -rsp "Пароль для веб-входа в Guacamole: " WEB_PASSWORD
    echo ""
    read -rsp "Повторите пароль:            " WEB_PASSWORD2
    echo ""
    if [[ "$WEB_PASSWORD" == "$WEB_PASSWORD2" && -n "$WEB_PASSWORD" ]]; then
        break
    fi
    warn "Пароли не совпадают или пустые. Попробуйте ещё раз."
done

# ─── RDP-пароль ───

while true; do
    read -rsp "Пароль для RDP-доступа к хосту: " RDP_PASSWORD
    echo ""
    read -rsp "Повторите пароль:               " RDP_PASSWORD2
    echo ""
    if [[ "$RDP_PASSWORD" == "$RDP_PASSWORD2" && -n "$RDP_PASSWORD" ]]; then
        break
    fi
    warn "Пароли не совпадают или пустые. Попробуйте ещё раз."
done

# ─── RDP-логин ───

read -rp "Логин для RDP-доступа [guac-user]: " RDP_USER
RDP_USER="${RDP_USER:-guac-user}"

# ─── Порт ───

echo ""
echo "Порты 80 и 8080 часто забанены хостинг-провайдерами."
echo "Рекомендую 8443 — он редко блокируется."
echo ""
read -rp "Порт для веб-интерфейса Guacamole [8443]: " GUAC_PORT
GUAC_PORT="${GUAC_PORT:-8443}"

# Простая валидация: число от 1 до 65535
if ! [[ "$GUAC_PORT" =~ ^[0-9]+$ ]] || (( GUAC_PORT < 1 || GUAC_PORT > 65535 )); then
    die "Некорректный порт: $GUAC_PORT. Должен быть числом от 1 до 65535."
fi

# ─── IP VPN-интерфейса ───

echo ""
echo "Сетевые интерфейсы:"
ip -4 -br addr show | grep -v '^lo ' || true
echo ""

# Пробуем угадать VPN-интерфейс
VPN_IFACE=$(ip -4 addr show | grep -E 'tun|tap|wg' -B1 | grep -oP '(?<=\d: )\S+' | head -1 || true)
if [[ -n "$VPN_IFACE" ]]; then
    VPN_IP_DETECTED=$(ip -4 addr show "$VPN_IFACE" | grep -oP '(?<=inet\s)\S+' | head -1 || true)
fi

if [[ -n "${VPN_IP_DETECTED:-}" ]]; then
    read -rp "IP VPN-интерфейса для прослушивания [$VPN_IP_DETECTED]: " VPN_IP
    VPN_IP="${VPN_IP:-$VPN_IP_DETECTED}"
else
    while true; do
        read -rp "IP VPN-интерфейса для прослушивания: " VPN_IP
        [[ -n "$VPN_IP" ]] && break
        warn "IP не может быть пустым."
    done
fi

# ─── Итоговая сводка ───

echo ""
echo "──────────────────────────────────────────────"
echo "  Параметры:"
echo "    Веб-порт:       ${GUAC_PORT}"
echo "    VPN IP:          ${VPN_IP}"
echo "    RDP-логин:       ${RDP_USER}"
echo "    Веб-логин:       admin"
echo "──────────────────────────────────────────────"
echo ""
read -rp "Продолжить? [Y/n] " CONFIRM
[[ "$CONFIRM" =~ ^[Nn] ]] && die "Отменено."

# ─────────────────────────────────────────────────────
# 3. Установка Docker, если нет
# ─────────────────────────────────────────────────────

if ! command -v docker &>/dev/null; then
    log "Устанавливаю Docker..."
    sudo dnf config-manager --add-repo \
        https://download.docker.com/linux/rhel/docker-ce.repo
    sudo dnf install -y docker-ce docker-compose-plugin
    sudo systemctl enable --now docker
    log "Docker установлен и запущен."
else
    log "Docker уже установлен: $(docker --version)"
fi

if ! groups | grep -q docker; then
    sudo usermod -aG docker "$USER"
    warn "Пользователь добавлен в группу docker."
    warn "После завершения скрипта перезайдите в сессию или выполните: newgrp docker"
fi

# ─────────────────────────────────────────────────────
# 4. Включение GNOME Remote Desktop (RDP-сервер)
# ─────────────────────────────────────────────────────

log "Настраиваю GNOME Remote Desktop..."

if ! command -v grctl &>/dev/null; then
    die "grctl не найден. Установите GNOME Remote Desktop: sudo dnf install -y gnome-remote-desktop"
fi

sudo grctl rdp enable 2>/dev/null || true
sudo grctl rdp set-credentials "$RDP_USER" "$RDP_PASSWORD" 2>/dev/null || {
    warn "Не удалось задать учётные данные через grctl."
    warn "Сделайте это вручную: sudo grctl rdp set-credentials $RDP_USER <пароль>"
}

if sudo ss -tlnp | grep -q ':3389'; then
    log "RDP-сервер слушает порт 3389."
else
    warn "RDP-сервер, возможно, не запущен."
    warn "Попробуйте: systemctl --user restart gnome-remote-desktop"
fi

# ─────────────────────────────────────────────────────
# 5. Конфигурация Guacamole
# ─────────────────────────────────────────────────────

GUAC_DIR="$HOME/guacamole"
log "Создаю каталог $GUAC_DIR..."
mkdir -p "$GUAC_DIR"

# --- docker-compose.yml ---
log "Пишу docker-compose.yml..."
cat > "$GUAC_DIR/docker-compose.yml" << COMPOSE_EOF
services:
  guacd:
    image: guacamole/guacd:latest
    restart: unless-stopped

  guacamole:
    image: guacamole/guacamole:latest
    restart: unless-stopped
    ports:
      - "${VPN_IP}:${GUAC_PORT}:8080"
    extra_hosts:
      - "host.docker.internal:host-gateway"
    environment:
      GUACD_HOSTNAME: guacd
      EXTENSION_PRIORITY: "*"
    volumes:
      - ./user-mapping.xml:/etc/guacamole/user-mapping.xml:ro
    depends_on:
      - guacd
COMPOSE_EOF

# --- bcrypt-хеш ---
log "Генерирую bcrypt-хеш пароля..."
BCRYPT_HASH=$(docker run --rm guacamole/guacamole \
    /opt/guacamole/bin/encrypt-password.sh bcrypt <<< "$WEB_PASSWORD" 2>/dev/null)

if [[ -z "$BCRYPT_HASH" ]]; then
    die "Не удалось сгенерировать bcrypt-хеш. Проверьте доступность образа guacamole/guacamole."
fi

# --- user-mapping.xml ---
log "Пишу user-mapping.xml..."
cat > "$GUAC_DIR/user-mapping.xml" << XML_EOF
<user-mapping>
    <authorize
        username="admin"
        password="${BCRYPT_HASH}"
        encoding="bcrypt">

        <connection name="VPS Desktop">
            <protocol>rdp</protocol>
            <param name="hostname">host.docker.internal</param>
            <param name="port">3389</param>
            <param name="username">${RDP_USER}</param>
            <param name="password">${RDP_PASSWORD}</param>
            <param name="ignore-cert">true</param>
            <param name="security">rdp</param>
        </connection>

    </authorize>
</user-mapping>
XML_EOF

chmod 600 "$GUAC_DIR/user-mapping.xml"

# ─────────────────────────────────────────────────────
# 6. Запуск
# ─────────────────────────────────────────────────────

log "Загружаю образы и запускаю контейнеры..."
cd "$GUAC_DIR"

if groups | grep -q docker; then
    docker compose up -d
else
    sudo docker compose up -d
fi

sleep 3

if docker compose ps 2>/dev/null | grep -q 'Up'; then
    log "Контейнеры запущены."
else
    warn "Что-то пошло не так. Проверьте:"
    warn "  cd $GUAC_DIR && docker compose ps"
    warn "  docker compose logs"
fi

# ─────────────────────────────────────────────────────
# 7. Готово
# ─────────────────────────────────────────────────────

echo ""
echo "══════════════════════════════════════════════"
echo "  Установка завершена"
echo "══════════════════════════════════════════════"
echo ""
echo "  Откройте в браузере:"
echo "    http://${VPN_IP}:${GUAC_PORT}/guacamole"
echo ""
echo "  Логин:  admin"
echo "  Пароль: (тот, что вы ввели)"
echo ""
echo "  Логи контейнеров:"
echo "    cd $GUAC_DIR && docker compose logs -f"
echo ""
echo "══════════════════════════════════════════════"
