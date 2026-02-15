import java.io.*;
import java.net.*;
import java.util.Scanner;

public class Client {
    private static final String SERVER_HOST = "server";
    private static final int SERVER_PORT = 8080;
    
    private Socket socket;
    private BufferedReader in;
    private PrintWriter out;
    private Scanner scanner;
    private Thread listenerThread;
    
    public Client() {
        scanner = new Scanner(System.in);
    }
    
    public void connetti() {
        try {
            socket = new Socket(SERVER_HOST, SERVER_PORT);
            in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
            out = new PrintWriter(socket.getOutputStream(), true);
            
          
          
            System.out.println(" FORZA 4 - CLIENT");
            System.out.println("Connesso al server!\n");
            
            avviaListener();
            
        } catch (IOException e) {
            System.err.println("Errore di connessione al server: " + e.getMessage());
            System.exit(1);
        }
    }
    
    private void avviaListener() {
        listenerThread = new Thread(() -> {
            try {
                String messaggio;
                while ((messaggio = in.readLine()) != null) {
                    gestisciMessaggioServer(messaggio);
                }
            } catch (IOException e) {
                if (!socket.isClosed()) {
                    System.err.println("\nConnessione al server persa.");
                }
            }
        });
        listenerThread.start();
    }
    
    private void gestisciMessaggioServer(String messaggio) {
    if (messaggio.startsWith(">")) {
        String contenuto = messaggio.substring(1).trim();
        if (!contenuto.isEmpty()) {
            System.out.println(contenuto);
        }
    } else if (messaggio.contains("NOTIFICA")) {
        System.out.println("\n" + messaggio);
    } else if (messaggio.contains("È il tuo turno")) {
        System.out.println("\n" + messaggio);
    } else if (messaggio.contains("Aspetta")) {
        System.out.println(messaggio);
    } else {
        System.out.println(messaggio);
    }
}
    
    public void gioca() {
        try {
            Thread.sleep(500);
            
            while (true) {
                if (scanner.hasNextLine()) {
                    String comando = scanner.nextLine().trim();
                    
                    if (comando.isEmpty()) {
                        continue;
                    }
                    
                    if (comando.equalsIgnoreCase("HELP") || comando.equals("?")) {
                        mostraAiuto();
                        continue;
                    }
                    
                    if (comando.equalsIgnoreCase("ESCI")) {
                        out.println("ESCI");
                        Thread.sleep(200);
                        break;
                    }
                    
                    out.println(comando);
                }
            }
            
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } finally {
            chiudiConnessione();
        }
    }
    
    private void mostraAiuto() {
    System.out.println("\n=== COMANDI DISPONIBILI ===");
    System.out.println("CREA           - Crea una nuova partita");
    System.out.println("LISTA          - Mostra partite disponibili");
    System.out.println("ENTRA <id>     - Richiedi di unirti alla partita");
    System.out.println("ACCETTA <id>   - Accetta richiesta (creatore)");
    System.out.println("RIFIUTA <id>   - Rifiuta richiesta (creatore)");
    System.out.println("0-6            - Scegli colonna durante il gioco");
    System.out.println("HELP o ?       - Mostra questo messaggio");
    System.out.println("ESCI           - Disconnetti dal server");
    System.out.println("===========================\n");
}
    
    private void chiudiConnessione() {
        try {
            if (listenerThread != null) {
                listenerThread.interrupt();
            }
            if (socket != null && !socket.isClosed()) {
                socket.close();
            }
            if (scanner != null) {
                scanner.close();
            }
            System.out.println("\nConnessione chiusa. Arrivederci!");
        } catch (IOException e) {
            System.err.println("Errore durante la chiusura: " + e.getMessage());
        }
    }
    
    public static void main(String[] args) {
        Client client = new Client();
        client.connetti();
        client.gioca();
    }
}