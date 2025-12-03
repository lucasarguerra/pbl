build:
	@gcc -c main.c -std=c99 -o main.o
	@gcc -c funcoes.s -o funcoes.o
	@gcc main.o funcoes.o -o processamento_imagem -lpthread

run:
	sudo ./processamento_imagem

clean:
	@rm -f main.o funcoes.o processamento_imagem
	@echo "Arquivos limpos!"

help:
	@echo ""
	@echo "  Comandos disponíveis:"
	@echo "  make build  - Compila o programa (gera processamento_imagem)"
	@echo "  make run    - Executa o programa (usa sudo)"
	@echo "  make clean  - Remove arquivos objeto e executável"
	@echo "  make help   - Mostra esta mensagem de ajuda"
	@echo ""
