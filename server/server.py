from flask import Flask, request, jsonify
from tinydb import TinyDB, Query
from flask_cors import CORS
import os
import json
import uuid
import subprocess
import platform

app = Flask(__name__)
CORS(app)

# Configurazione
UPLOAD_FOLDER = 'uploads'
app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# Database targhe
plates_table = TinyDB('plates.json').table('_default')

def is_plate_authorized(plate):
    Plate = Query()
    result = plates_table.search(Plate.plate == plate.upper())
    return bool(result)

@app.before_request
def clear_uploads():
    for f in os.listdir(UPLOAD_FOLDER):
        try:
            os.remove(os.path.join(UPLOAD_FOLDER, f))
        except:
            pass

@app.route("/")
def index():
    print("📡 Richiesta GET /")
    return jsonify({"status": "online"})

@app.route("/check", methods=["GET"])
def check_plate():
    print("🔍 Richiesta GET /check")
    plate = request.args.get("plate", "").upper()
    print(f"▶️ Targa ricevuta: {plate}")
    Plate = Query()
    result = plates_table.search(Plate.plate == plate)
    return jsonify(bool(result))

@app.route("/upload", methods=["POST"])
def upload_image():
    print("🖼️ Richiesta POST /upload")

    if 'imageFile' not in request.files:
        print("❌ Nessun file ricevuto con chiave 'imageFile'")
        return jsonify({'error': 'No imageFile part in request'}), 400

    image = request.files['imageFile']
    if image.filename == '':
        print("❌ Nome file vuoto")
        return jsonify({'error': 'No selected file'}), 400

    country_code = request.form.get("countrycode", "eu")
    print(f"🌍 Country code ricevuto: {country_code}")

    filename = f"{uuid.uuid4()}.jpg"
    print(f"📁 Filename: {filename}")
    filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
    print(f"📁 Filepath: {filepath}")
    image.save(filepath)
    print(f"✅ Immagine salvata in: {filepath}")
    
    # Ottieni il path assoluto dell'upload folder
    upload_abs_path = os.path.abspath(UPLOAD_FOLDER)
    print(f"📁 Upload absolute path: {upload_abs_path}")
    
    # Verifica che il file esista
    if not os.path.exists(filepath):
        print("❌ File non trovato dopo il salvataggio")
        return jsonify({'error': 'File not saved correctly'}), 500
    
    try:
        # Comando Docker con debug migliorato
        # Converti il path Windows per Docker
        volume_path = upload_abs_path.replace('\\', '/')
        if len(volume_path) > 1 and volume_path[1] == ':':
            # C:/path -> /c/path
            volume_path = f"/{volume_path[0].lower()}{volume_path[2:]}"
            
        docker_cmd = [
            "docker", "run", "--rm",
            "-v", f"{volume_path}:/data:ro",
            "openalpr/openalpr",
            "-c", country_code,
            "--json",
            f"/data/{filename}"
        ]

        print(f"⚙️ Comando Docker: {' '.join(docker_cmd)}")
        
        # Test se Docker è disponibile
        try:
            test_result = subprocess.run(["docker", "--version"], 
                                       stdout=subprocess.PIPE, 
                                       stderr=subprocess.PIPE, 
                                       text=True, 
                                       timeout=5)
            if test_result.returncode != 0:
                print("❌ Docker non disponibile")
                return jsonify({'error': 'Docker not available'}), 500
            print(f"✅ Docker version: {test_result.stdout.strip()}")
        except subprocess.TimeoutExpired:
            print("❌ Docker timeout")
            return jsonify({'error': 'Docker timeout'}), 500
        except FileNotFoundError:
            print("❌ Docker non trovato")
            return jsonify({'error': 'Docker not found'}), 500

        # Test se l'immagine OpenALPR esiste
        try:
            image_check = subprocess.run(["docker", "images", "openalpr/openalpr"], 
                                       stdout=subprocess.PIPE, 
                                       stderr=subprocess.PIPE, 
                                       text=True, 
                                       timeout=10)
            if "openalpr/openalpr" not in image_check.stdout:
                print("❌ Immagine OpenALPR non trovata, scaricamento...")
                pull_result = subprocess.run(["docker", "pull", "openalpr/openalpr"], 
                                           stdout=subprocess.PIPE, 
                                           stderr=subprocess.PIPE, 
                                           text=True, 
                                           timeout=120)
                if pull_result.returncode != 0:
                    print(f"❌ Errore nel pull: {pull_result.stderr}")
                    return jsonify({'error': 'Failed to pull OpenALPR image'}), 500
                print("✅ Immagine OpenALPR scaricata")
        except subprocess.TimeoutExpired:
            print("❌ Timeout nel check dell'immagine")
            return jsonify({'error': 'Docker image check timeout'}), 500

        print("⚙️ Esecuzione OpenALPR...")
        result = subprocess.run(docker_cmd, 
                              stdout=subprocess.PIPE, 
                              stderr=subprocess.PIPE, 
                              text=True, 
                              timeout=30)

        print(f"🔧 Return code: {result.returncode}")
        print(f"📤 STDOUT: {result.stdout}")
        print(f"📤 STDERR: {result.stderr}")

        if result.returncode != 0:
            print("❌ Errore: Docker/OpenALPR failed")
            error_msg = result.stderr if result.stderr else "Unknown Docker error"
            print(f"❌ Dettaglio errore: {error_msg}")
            return jsonify({'error': f'Docker/OpenALPR failed: {error_msg}'}), 500

        if not result.stdout.strip():
            print("❌ Output vuoto da OpenALPR")
            return jsonify({'error': 'Empty output from OpenALPR'}), 500

        print("✅ Output ricevuto")
        try:
            data = json.loads(result.stdout)
        except json.JSONDecodeError as e:
            print(f"❌ Errore JSON: {e}")
            print(f"❌ Output ricevuto: {result.stdout}")
            return jsonify({'error': 'Invalid JSON from OpenALPR'}), 500

        plate = ""
        if data.get("results"):
            plate = data["results"][0].get("plate", "").upper()
            print(f"📌 Targa riconosciuta: {plate}")
        else:
            print("❌ Nessuna targa trovata")

        if is_plate_authorized(plate):
            print("✅ Targa autorizzata")
            return jsonify({"access": 0, "plate": plate}), 200
        else:
            print("❌ Targa non autorizzata")
            return jsonify({"access": 1, "plate": plate}), 200

    except subprocess.TimeoutExpired:
        print("❌ Timeout OpenALPR")
        return jsonify({'error': 'OpenALPR timeout'}), 500
    except Exception as e:
        print(f"❌ Errore interno: {e}")
        return jsonify({'error': f'Internal server error: {str(e)}'}), 500

@app.route("/check_password", methods=["POST"])
def check_password():
    print("🔐 Richiesta POST /check_password")
    try:
        data = request.get_json()
        password = data.get("password")
        Plate = Query()
        result = plates_table.search((Plate.password == password))
        return jsonify(bool(result))
    except Exception as e:
        print(f"❌ Errore: {e}")
        return jsonify(False)

@app.route("/check_admin", methods=["POST"])
def check_admin():
    print("🔐 Richiesta POST /check_admin")
    try:
        data = request.get_json()
        password = data.get("password")
        Plate = Query()
        result = plates_table.search((Plate.password == password) & (Plate.admin == True))
        return jsonify(bool(result))
    except Exception as e:
        print(f"❌ Errore: {e}")
        return jsonify(False)

@app.route("/addplate", methods=["POST"])
def add_plate():
    print("➕ Richiesta POST /addplate")
    content = request.json
    plate = content.get("plate", "").upper()
    password = content.get("password", "")
    is_admin = content.get("admin", False)

    if not plate or not password:
        return jsonify({"error": "Missing plate or password"}), 400

    Plate = Query()
    if plates_table.contains(Plate.plate == plate):
        return jsonify({"error": "Plate already exists"}), 409

    plates_table.insert({
        "plate": plate,
        "password": password,
        "admin": is_admin
    })

    return jsonify({"status": "plate added", "plate": plate})

@app.route("/removeplate/<plate>", methods=["DELETE"])
def remove_plate(plate):
    print(f"🗑️ Richiesta DELETE /removeplate/{plate}")
    Plate = Query()
    removed = plates_table.remove(Plate.plate == plate.upper())
    return jsonify({"removed": bool(removed)})

@app.route("/debug_db", methods=["GET"])
def debug_db():
    return jsonify(plates_table.all())

if __name__ == "__main__":
    print("🚀 Avvio server Flask su 0.0.0.0:5000")
    print(f"📂 Upload folder: {UPLOAD_FOLDER}")
    print("📋 Targhe caricate:")
    for record in plates_table.all():
        print(f"  → {record}")
    app.run(host='0.0.0.0', port=5000, debug=True)