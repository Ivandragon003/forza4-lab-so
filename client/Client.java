import java.io.*;
import java.net.*;
import java.util.Scanner;

public class Client {
    private static final String SERVER_HOST = "server";
    private static final int SERVER_PORT = 8080;
    
    public static void main(String[] args) {
        try {
            Socket socket = new Socket(SERVER_HOST, SERVER_PORT);
            System.out.println("Connesso al server!");
            
            BufferedReader in = new BufferedReader(
                new InputStreamReader(socket.getInputStream())
            );
            PrintWriter out = new PrintWriter(socket.getOutputStream(), true);
            Scanner scanner = new Scanner(System.in);
            
            // Ricevi messaggio di benvenuto
            System.out.println(in.readLine());
            
            // Loop comunicazione
            while (true) {
                System.out.print("Inserisci comando: ");
                String command = scanner.nextLine();
                
                if (command.equalsIgnoreCase("exit")) {
                    break;
                }
                
                out.println(command);
                System.out.println("Server: " + in.readLine());
            }
            
            socket.close();
            scanner.close();
            
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}