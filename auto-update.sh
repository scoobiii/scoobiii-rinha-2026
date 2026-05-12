#!/bin/bash
set -e

# Cores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Configurações
REPO_DIR="$(pwd)"
BACKUP_DIR="../backups_$(basename "$REPO_DIR")"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
COMMIT_MSG="${1:-"Automated update: $TIMESTAMP"}"

# Obtém a branch atual de forma confiável
BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
if [ -z "$BRANCH" ] || [ "$BRANCH" = "HEAD" ]; then
    BRANCH=$(git symbolic-ref --short HEAD 2>/dev/null || echo "detached")
fi

echo -e "${GREEN}🚀 Iniciando automação do repositório...${NC}"
echo -e "${YELLOW}Branch atual: $BRANCH${NC}"

# 1. Backup (exclui .git)
echo -e "${GREEN}📦 Criando backup do diretório...${NC}"
mkdir -p "$BACKUP_DIR"
BACKUP_FILE="$BACKUP_DIR/${TIMESTAMP}_backup.tar.gz"
tar --exclude='.git' -czf "$BACKUP_FILE" . 2>/dev/null || {
    echo -e "${YELLOW}⚠️  Nada a adicionar ao backup (pasta vazia?)${NC}"
}
echo -e "${GREEN}✅ Backup salvo em: $BACKUP_FILE${NC}"

# 2. Fetch e pull com stash automático
echo -e "${GREEN}🔄 Atualizando repositório remoto...${NC}"
git fetch origin

STASHED=false
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo -e "${YELLOW}📦 Guardando mudanças locais (stash)...${NC}"
    git stash push -m "auto-stash antes do pull" --include-untracked
    STASHED=true
else
    echo -e "${GREEN}✅ Nenhuma mudança local para stash.${NC}"
fi

# Só faz pull se estiver nas branches esperadas (main ou submission)
if [ "$BRANCH" = "main" ] || [ "$BRANCH" = "submission" ]; then
    echo -e "${GREEN}📥 Puxando alterações de origin/$BRANCH com rebase...${NC}"
    git pull --rebase origin "$BRANCH" || {
        echo -e "${RED}❌ Falha no rebase. Tentando restaurar stash...${NC}"
        [ "$STASHED" = true ] && git stash pop
        exit 1
    }
else
    echo -e "${YELLOW}⚠️  Branch não é main/submission. Pulando pull.${NC}"
fi

# Restaura stash se existir e se não houver conflitos (já tratado acima)
if [ "$STASHED" = true ]; then
    echo -e "${GREEN}📦 Restaurando mudanças locais (stash pop)...${NC}"
    git stash pop || {
        echo -e "${RED}❌ Conflito ao restaurar stash. Resolva manualmente.${NC}"
        exit 1
    }
fi

# 3. Adicionar todas as mudanças (incluindo untracked)
echo -e "${GREEN}➕ Adicionando arquivos modificados/novos...${NC}"
git add .

# 4. Commit se houver mudanças
if git diff --cached --quiet; then
    echo -e "${YELLOW}⚠️  Nenhuma alteração para commit.${NC}"
else
    git commit -m "$COMMIT_MSG"
    echo -e "${GREEN}✅ Commit realizado: $COMMIT_MSG${NC}"
fi

# 5. Push
echo -e "${GREEN}☁️ Enviando para o repositório remoto...${NC}"
git push origin "$BRANCH"
echo -e "${GREEN}✅ Push realizado.${NC}"

# 6. Gerar documentação automática (changelog)
LOG_FILE="$REPO_DIR/docs/auto_changelog.md"
mkdir -p "$REPO_DIR/docs"
echo "## Changelog Automatizado - $TIMESTAMP" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"
echo "### Alterações no commit \`$(git rev-parse --short HEAD)\`" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"
echo '```' >> "$LOG_FILE"
git diff --name-only HEAD~1 HEAD 2>/dev/null | sed 's/^/- /' >> "$LOG_FILE" || echo "- Nenhuma mudança recente" >> "$LOG_FILE"
echo '```' >> "$LOG_FILE"
echo "" >> "$LOG_FILE"
echo "---" >> "$LOG_FILE"

echo -e "${GREEN}📝 Registro de alterações salvo em: $LOG_FILE${NC}"

# 7. (Opcional) Rodar testes se existir script de teste
if [ -f "test.sh" ]; then
    echo -e "${GREEN}🧪 Executando testes...${NC}"
    ./test.sh
fi

echo -e "${GREEN}🏁 Automação concluída com sucesso!${NC}"
