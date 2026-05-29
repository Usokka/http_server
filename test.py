import subprocess
import socket
import time
import sys
import urllib.request
import urllib.error

PORT = 8999
URL = f"http://localhost:{PORT}"

def run_server():
    """Lance le serveur C-Web en tâche de fond."""
    print("🚀 Démarrage du serveur pour les tests...")
    # On capture stdout et stderr pour éviter de polluer la console de test
    process = subprocess.Popen(["./cweb", str(PORT)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.5) # On laisse le temps au serveur de bind le port
    return process

def test_normal_request():
    """Cas 1 : Requête GET standard et valide."""
    print("⏳ Test 1: Requête GET standard...")
    try:
        response = urllib.request.urlopen(f"{URL}/index.html")
        content = response.read().decode('utf-8')
        if "Bienvenue sur C-Web" in content and response.status == 200:
            print("✅ Test 1 Réussi (200 OK)")
            return True
    except Exception as e:
        print(f"❌ Test 1 Échoué: {e}")
    return False

def test_directory_traversal():
    """Cas 2 : Sécurité - Tentative de remontée de répertoire classique."""
    print("⏳ Test 2: Attaque Directory Traversal (../)...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        s.sendall(b"GET /../../etc/passwd HTTP/1.1\r\n\r\n")
        response = s.recv(1024).decode('utf-8')
        s.close()
        if "404 Not Found" in response:
            print("✅ Test 2 Réussi (Attaque basique bloquée)")
            return True
        else:
            print("❌ Test 2 Échoué (Le serveur a peut-être laissé passer ou répondu autre chose)")
    except Exception as e:
        print(f"❌ Test 2 Échoué: {e}")
    return False

def test_directory_traversal_encoded():
    """Cas 2b : Sécurité - Tentative de remontée via URL encodée."""
    print("⏳ Test 2b: Attaque Directory Traversal encodée (%2e%2e)...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        # %2e%2e%2f correspond à ../
        s.sendall(b"GET /%2e%2e/%2e%2e/etc/passwd HTTP/1.1\r\n\r\n")
        response = s.recv(1024).decode('utf-8')
        s.close()
        if "404 Not Found" in response:
            print("✅ Test 2b Réussi (Attaque encodée bloquée)")
            return True
        else:
            print("❌ Test 2b Échoué (Vulnérabilité potentielle ou mauvaise réponse)")
    except Exception as e:
        print(f"❌ Test 2b Échoué: {e}")
    return False

def test_slow_client():
    """Cas 3 : Non-bloquant - Client lent qui fragmente sa requête de base."""
    print("⏳ Test 3: Client non-bloquant lent (2 morceaux)...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        s.sendall(b"GET /ind")
        time.sleep(0.4) 
        s.sendall(b"ex.html HTTP/1.1\r\n\r\n")
        response = s.recv(1024).decode('utf-8')
        s.close()
        if "200 OK" in response:
            print("✅ Test 3 Réussi (Requête fragmentée reconstruite)")
            return True
    except Exception as e:
        print(f"❌ Test 3 Échoué: {e}")
    return False

def test_404_not_found():
    """Cas 4 : Fichier inexistant."""
    print("⏳ Test 4: Demande d'un fichier inexistant...")
    try:
        urllib.request.urlopen(f"{URL}/introuvable.html")
    except urllib.error.HTTPError as e:
        if e.code == 404:
            print("✅ Test 4 Réussi (404 retourné)")
            return True
    print("❌ Test 4 Échoué")
    return False

def test_malformed_request():
    """Cas 5 : Sécurité/Robustesse - Requête complètement invalide."""
    print("⏳ Test 5: Requête malformée...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        s.sendall(b"N_IMPORTE_QUOI_PAS_HTTP\r\n\r\n")
        response = s.recv(1024).decode('utf-8')
        s.close()
        print("✅ Test 5 Réussi (Le serveur a géré le déchet sans crasher)")
        return True
    except Exception as e:
        print(f"❌ Test 5 Échoué: {e}")
    return False

def test_501_not_implemented():
    """Cas 6 : Protocole - Méthode POST non supportée."""
    print("⏳ Test 6: Méthode POST (501)...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        s.sendall(b"POST /index.html HTTP/1.1\r\n\r\n")
        response = s.recv(1024).decode('utf-8')
        s.close()
        if "501" in response:
            print("✅ Test 6 Réussi (501 Not Implemented)")
            return True
    except Exception as e:
        print(f"❌ Test 6 Échoué: {e}")
    return False

def test_slow_client_fragmentation():
    """Cas 7 : Non-bloquant - Client ultra lent (3 morceaux & EAGAIN)."""
    print("⏳ Test 7: Client non-bloquant agressif (3 morceaux & EAGAIN)...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        s.sendall(b"GET /ind")
        time.sleep(0.5)  
        s.sendall(b"ex.html HTTP/1.1\r\nHost: localhost\r\n")
        time.sleep(0.5)  
        s.sendall(b"\r\n")
        response = s.recv(4096).decode('utf-8')
        s.close()
        if "200 OK" in response and "Bienvenue sur C-Web" in response:
            print("✅ Test 7 Réussi : Requête fragmentée traitée avec succès !")
            return True
        else:
            print("❌ Test 7 Échoué : Réponse incomplète ou incorrecte.")
            return False
    except Exception as e:
        print(f"❌ Test 7 Échoué: {e}")
        return False

def test_request_too_long():
    """Cas 8 : Sécurité - Requête géante sans fin (Tentative de Buffer Overflow)."""
    print("⏳ Test 8: Grosse requête (Buffer Overflow protection)...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        # On envoie une méthode HTTP invalide de 5000 octets (supérieure à BUFFER_SIZE)
        giant_payload = b"A" * 5000 + b"\r\n\r\n"
        s.sendall(giant_payload)
        response = s.recv(1024).decode('utf-8')
        s.close()
        print("✅ Test 8 Réussi (Le serveur a encaissé la charge sans crasher)")
        return True
    except Exception as e:
        print(f"❌ Test 8 Échoué (Le serveur a potentiellement subi un SegFault) : {e}")
        return False

def test_target_is_directory():
    """Cas 9 : Robustesse - Demande d'un répertoire racine au lieu d'un fichier."""
    print("⏳ Test 9: Cible est un répertoire (ex: /www/)...")
    try:
        # On crée un sous-dossier de test pour s'assurer de sa présence
        subprocess.run(["mkdir", "-p", "www/images"], stdout=subprocess.DEVNULL)
        
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        s.sendall(b"GET /images HTTP/1.1\r\n\r\n")
        response = s.recv(1024).decode('utf-8')
        s.close()
        
        # Le serveur doit renvoyer 404 (ou 403), mais ne doit SURTOUT pas essayer d'ouvrir le dossier comme un fichier régulier
        if "404 Not Found" in response or "403" in response:
            print("✅ Test 9 Réussi (Répertoire rejeté proprement)")
            return True
        else:
            print("❌ Test 9 Échoué (Réponse suspecte lors de l'accès à un dossier)")
            return False
    except Exception as e:
        print(f"❌ Test 9 Échoué: {e}")
        return False

def test_head_method():
    """Cas 10 : Évolution - Test de la méthode HEAD."""
    print("⏳ Test 10: Méthode HEAD (En-têtes uniquement)...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", PORT))
        s.sendall(b"HEAD /index.html HTTP/1.1\r\n\r\n")
        response = s.recv(1024).decode('utf-8')
        s.close()
        
        # Actuellement, ton code renvoie un 501 pour tout ce qui n'est pas GET.
        # Ce test validera si le comportement attendu (501 ou gestion de HEAD) est respecté sans crash.
        if "501" in response or ("200 OK" in response and "<html>" not in response):
            print("✅ Test 10 Réussi (Comportement cohérent)")
            return True
    except Exception as e:
        print(f"❌ Test 10 Échoué: {e}")
    return False

if __name__ == "__main__":
    subprocess.run(["make", "init"], stdout=subprocess.DEVNULL)
    subprocess.run(["make"], stdout=subprocess.DEVNULL)
    
    server_process = run_server()
    success = True
    
    try:
        # Suite complète d'exécution
        if not test_normal_request(): success = False
        if not test_directory_traversal(): success = False
        if not test_directory_traversal_encoded(): success = False
        if not test_slow_client(): success = False
        if not test_404_not_found(): success = False
        if not test_malformed_request(): success = False
        if not test_501_not_implemented(): success = False
        if not test_slow_client_fragmentation(): success = False
        if not test_request_too_long(): success = False
        if not test_target_is_directory(): success = False
        if not test_head_method(): success = False
    finally:
        print("🛑 Arrêt du serveur...")
        server_process.terminate()
        server_process.wait()
        
    if success:
        print("\n🎉 TOUS LES TESTS SONT PASSÉS AVEC SUCCÈS !")
        sys.exit(0)
    else:
        print("\n💥 CERTAINS TESTS ONT ÉCHOUÉ... Vérifie les défauts listés ci-dessus.")
        sys.exit(1)