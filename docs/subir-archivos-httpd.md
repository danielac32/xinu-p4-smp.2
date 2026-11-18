# Subir y gestionar archivos por HTTP (httpd de Xinu)

Esta placa expone un servidor HTTP (`httpd`) que atiende la API de ficheros sobre la microSD. Permite listar, descargar, subir, borrar y gestionar directorios de forma remota (útil para reemplazar ELFs de apps sin extraer la tarjeta).

## Requisitos

- La placa debe tener red operativa y el `httpd` en ejecución (en esta configuración lo arranca `netboot`).
- Conectividad IP desde el host a la placa (ej. `192.168.1.106`).
- `curl` en el host.

En los ejemplos se usa `BASE=http://192.168.1.106`:

```bash
BASE="http://192.168.1.106"
```

## API de ficheros

Las rutas (`path`) son **absolutas dentro de la tarjeta SD** (ej. `/apps/snake/app.elf`). El servidor normaliza la ruta (colapsa `.`/`..` y no permite salir de la raíz de la SD).

| Operación | Método y endpoint | Notas |
|---|---|---|
| Listar directorio | `GET /api/files?path=/dir` | Devuelve JSON con `name`, `dir`, `size`. |
| Descargar fichero | `GET /api/download?path=/file` | Devuelve el contenido binario. |
| Subir fichero | `PUT /api/file?path=/file` | Cuerpo crudo (binario). Crea directorios padre si faltan. |
| Borrar fichero | `DELETE /api/file?path=/file` | Borra un fichero. |
| Crear directorio | `POST /api/mkdir?path=/dir` | Crea el directorio (y padres). |
| Borrar directorio | `POST /api/rmdir?path=/dir[&recursive=1]` | `recursive=1` elimina su contenido. |

Respuestas típicas (JSON): `{"ok":true,...}` o `{"ok":false,"error":"..."}`.

### Detalles importantes de `PUT /api/file`

- Requiere `Content-Length` (si falta: `411 Length Required`).
- Límite de tamaño: **16 MB** (`413 Payload Too Large`).
- La librería FAT del sistema no trunca al abrir en escritura: por eso el servidor **elimina el fichero previo** antes de escribir el nuevo (evita dejar “cola” del contenido/tamaño anterior).
- Si el cuerpo recibido no coincide con `Content-Length`, responde `{"error":"incomplete body"}` y elimina el fichero parcial.
- Crea los directorios padre que falten.

## Ejemplos básicos

### Listar un directorio

```bash
curl -s "$BASE/api/files?path=/apps"
curl -s "$BASE/api/files?path=/apps/snake"
```

### Descargar un fichero

```bash
curl -s -o app.elf.bak "$BASE/api/download?path=/apps/snake/app.elf"
```

### Subir un fichero

Dos formas habituales:

```bash
# 1) Desde fichero local (-T infiere y envía Content-Length)
curl -X PUT -T ./snake.elf "$BASE/api/file?path=/apps/snake/app.elf"

# 2) Enviando el cuerpo binario explícito (útil si -T da problemas)
curl -X PUT --data-binary @./snake.elf "$BASE/api/file?path=/apps/snake/app.elf"
```

Respuesta esperada: `{"ok":true,"path":"/apps/snake/app.elf","size":...}`.

### Borrar un fichero

```bash
curl -X DELETE "$BASE/api/file?path=/apps/snake/app.elf"
```

### Crear y borrar directorios

```bash
curl -X POST "$BASE/api/mkdir?path=/apps/miapp"
curl -X POST "$BASE/api/rmdir?path=/apps/miapp&recursive=1"
```

## Reemplazar un ELF de app (flujo recomendado)

Patrón seguro (como el usado en el árbol): borrar el fichero anterior, esperar un momento y subir el nuevo, luego verificar. Reemplaza `<app>` y la ruta local según corresponda:

```bash
BASE="http://192.168.1.106"
APP="snake"
LOCAL="./snake.elf"
REMOTE="/apps/$APP/app.elf"

# 1) Borrar el ELF anterior
curl -s -X DELETE "$BASE/api/file?path=$REMOTE"

# 2) Pequeña pausa (el servidor atiende secuencialmente)
sleep 1

# 3) Subir el nuevo (guardarlo como app.elf si así lo espera el launcher)
curl -s -X PUT -T "$LOCAL" "$BASE/api/file?path=$REMOTE"

# 4) Verificar tamaño/contenido del directorio
curl -s "$BASE/api/files?path=/apps/$APP"
```

> Nota: según el `app.txt`/configuración, el ejecutable puede llamarse `app.elf` u otro nombre (ej. el launcher usa `/apps/launcher/launcher.elf`). Sube al nombre/ruta que la app/arranque espera.

## Subir varias apps y limpiar las que sobran

```bash
BASE="http://192.168.1.106"
APPSRC="./build/apps"   # directorio con los .elf locales

# Borrar directorios de apps no deseadas (recursivo)
for d in ejemplo floattest; do
  curl -s -X POST "$BASE/api/rmdir?path=/apps/$d&recursive=1"
done

# Reemplazar un conjunto de apps (local <nombre>.elf -> remoto /apps/<nombre>/app.elf)
for name in calculadora flappy grabador musica photos settings snake tareas tetris tictac; do
  REMOTE="/apps/$name/app.elf"
  echo "== $name =="
  curl -s -X DELETE "$BASE/api/file?path=$REMOTE"
  sleep 1
  curl -s -X PUT -T "$APPSRC/$name/$name.elf" "$BASE/api/file?path=$REMOTE"
  curl -s "$BASE/api/files?path=/apps/$name"
done
```

## Solución de problemas

- **`{"error":"incomplete body"}`**: el cuerpo no llegó completo (red inestable o corte). El servidor borra el fichero parcial. Reintenta; si persiste con `-T`, prueba:
  ```bash
  curl -s -X PUT --data-binary @./archivo.elf "$BASE/api/file?path=/apps/app/app.elf"
  ```
  o en tubería: `cat archivo.elf | curl -s -X PUT --data-binary @- "$BASE/api/file?path=..."`. Aumenta el timeout (`-m 60`) para ficheros grandes.
- **`411 Length Required`**: la petición no envió `Content-Length` (asegúrate de usar `-T fichero` o `--data-binary @fichero`, no datos sin tamaño conocido).
- **`503 Service Unavailable` ("FAT not mounted")**: la microSD/FAT no está montada en la placa.
- **`409 Conflict`** ("path is a directory"): intentas subir sobre una ruta que es directorio.
- **Sin respuesta / timeout**: verifica conectividad (`ping <IP>`, `curl -s -o /dev/null -w "%{http_code}" $BASE/`) y que el `httpd` siga activo; el servidor atiende de forma secuencial, así que subidas grandes pueden tardar.
- Tras sobrescribir, conviene **listar/verificar** el `size` con `/api/files` para confirmar la subida.

## Referencia en el código

- Implementación de la API: `shell/httpapi.c` (endpoints `files`, `download`, `file` PUT/DELETE, `mkdir`, `rmdir`, `shot`).
- Despacho HTTP: `shell/xsh_httpd.c` (rutas bajo `/api/`).
