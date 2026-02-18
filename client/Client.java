import java.io.*;
import java.net.*;
import java.util.Scanner;

public class Client {
    private static final String SERVER_HOST = "server";
    private static final int SERVER_PORT = 8080;
    
    private static final String RESET = "\033[0m";
    private static final String RED = "\033[31m";
    private static final String YELLOW = "\033[33m";
    private static final String GREEN = "\033[32m";
    private static final String BLUE = "\033[36m";
    private static final String CYAN = "\033[96m";
    private static final String BOLD = "\033[1m";
    
    private Socket socket;
    private BufferedReader in;
    private PrintWriter out;
    private Scanner scanner;
    private Thread listenerThread;
    private volatile boolean chiusuraStampata = false;
    
    public Client() {
        scanner = new Scanner(System.in);
    }
    
    public void connetti() {
        try {
            socket = new Socket(SERVER_HOST, SERVER_PORT);
            in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
            out = new PrintWriter(socket.getOutputStream(), true);
            
            System.out.println(BLUE + "Connesso al server!" + RESET);
            
            avviaListener();
            registraShutdownHook();
            
        } catch (IOException e) {
            System.err.println("Errore di connessione: " + e.getMessage());
            System.exit(1);
        }
    }

    private void registraShutdownHook() {
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            try {
                if (out != null) {
                    out.println("ABBANDONA");
                    out.println("ESCI");
                    out.flush();
                }
            } catch (Exception ignored) {
            }
            chiudiConnessione();
        }));
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
                    System.err.println("\nConnessione persa.");
                }
            }
        });
        listenerThread.start();
    }
    
    private void gestisciMessaggioServer(String messaggio) {
    
        if (messaggio.trim().equals(">")) {
            return;
        }
        
       
        if (messaggio.startsWith(">")) {
            messaggio = messaggio.substring(1).trim();
            if (messaggio.isEmpty()) {
                return;
            }
        }
        
        
        if (messaggio.contains("HAI VINTO")) {
            System.out.println(GREEN + BOLD + messaggio + RESET);
        } else if (messaggio.contains("tuo turno") && messaggio.contains("Scegli una colonna")) {
            System.out.println(GREEN + BOLD + messaggio + RESET);
        } else if (messaggio.contains("Aspetta il tuo turno")) {
            System.out.println(CYAN + messaggio + RESET);
        } else if (messaggio.contains("perso")) {
            System.out.println(RED + messaggio + RESET);
        } else if (messaggio.contains("PAREGGIO")) {
            System.out.println(YELLOW + BOLD + messaggio + RESET);
        } else if (messaggio.contains("NOTIFICA")) {
            System.out.println(BLUE + BOLD + messaggio + RESET);
        } else if (eGriglia(messaggio)) {
            System.out.println(coloraGriglia(messaggio));
        } else {
            System.out.println(messaggio);
        }
    }
    
    private boolean eGriglia(String testo) {
        return testo.matches(".*[.RG]\\s+[.RG].*") || 
               testo.matches("\\d\\s+\\d\\s+\\d.*");
    }
    
    private String coloraGriglia(String riga) {
        if (riga.matches("\\d\\s+\\d\\s+\\d.*")) {
            return riga;
        }
        
    
        StringBuilder risultato = new StringBuilder();
        for (int i = 0; i < riga.length(); i++) {
            char c = riga.charAt(i);
            
            if (c == 'R' && (i == 0 || riga.charAt(i-1) == ' ') && 
                (i == riga.length()-1 || riga.charAt(i+1) == ' ')) {
                risultato.append(RED).append('R').append(RESET);
            } else if (c == 'G' && (i == 0 || riga.charAt(i-1) == ' ') && 
                       (i == riga.length()-1 || riga.charAt(i+1) == ' ')) {
                risultato.append(YELLOW).append('G').append(RESET);
            } else {
                risultato.append(c);
            }
        }
        
        return risultato.toString();
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
                    
                    if (comando.equalsIgnoreCase("ABBANDONA")) {
                        out.println("ABBANDONA");
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
            if (!chiusuraStampata) {
                chiusuraStampata = true;
                System.out.println("\nConnessione chiusa.");
            }
        } catch (IOException e) {
            System.err.println("Errore chiusura: " + e.getMessage());
        }
    }
    
    public static void main(String[] args) {
        Client client = new Client();
        client.connetti();
        client.gioca();
    }
}


