package cl.inacap.comederogato;

import android.os.Bundle;
import android.util.Log;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.TimePicker;
import android.widget.Toast;

import androidx.activity.EdgeToEdge;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import com.google.android.material.progressindicator.LinearProgressIndicator;
import com.google.firebase.database.DataSnapshot;
import com.google.firebase.database.DatabaseError;
import com.google.firebase.database.DatabaseReference;
import com.google.firebase.database.FirebaseDatabase;
import com.google.firebase.database.Query;
import com.google.firebase.database.ValueEventListener;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

public class MainActivity extends AppCompatActivity {

    private static final String TAG = "ComederoGato";

    // ---------------------------------------------------------------------
    // ENLACE DE FIREBASE REALTIME DATABASE
    // Déjalo vacío ("") para usar el enlace que trae app/google-services.json.
    // Si Android Studio muestra "Failed to get FirebaseDatabase instance",
    // pega aquí el enlace exacto de la consola, por ejemplo:
    //   "https://comedero-gato-1a2b3-default-rtdb.firebaseio.com"
    // ---------------------------------------------------------------------
    private static final String DATABASE_URL = "";

    // Rutas de los nodos (deben coincidir con el firmware de la ESP32)
    private static final String RUTA_ACTIVAR_MANUAL = "control/activarManual";
    private static final String RUTA_HORARIO_FIJO = "control/horarioFijo";
    private static final String RUTA_NIVEL_COMIDA = "monitoreo/nivelComida";
    private static final String RUTA_HISTORIAL = "historial_comidas";
    private static final int MAX_REGISTROS_HISTORIAL = 50;

    // Referencias a Firebase
    private DatabaseReference refActivarManual;
    private DatabaseReference refHorarioFijo;
    private DatabaseReference refNivelComida;
    private Query consultaHistorial;

    // Listeners en tiempo real
    private ValueEventListener listenerNivel;
    private ValueEventListener listenerHistorial;
    private ValueEventListener listenerHorario;
    private ValueEventListener listenerManual;

    // Vistas
    private TextView tvNivelComida;
    private TextView tvEstadoNivel;
    private LinearProgressIndicator pbNivelComida;
    private Button btnAlimentarManual;
    private TimePicker timePickerHorario;
    private TextView tvHorarioActual;
    private TextView tvTotalHistorial;

    // Historial
    private final List<String> registrosHistorial = new ArrayList<>();
    private HistorialAdapter adaptadorHistorial;

    // Solo se ajusta el TimePicker con el valor de la nube la primera vez
    private boolean horarioCargadoEnPicker = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        setContentView(R.layout.activity_main);
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.main), (v, insets) -> {
            Insets barras = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            v.setPadding(barras.left, barras.top, barras.right, barras.bottom);
            return insets;
        });

        enlazarVistas();
        inicializarFirebase();
        crearListeners();

        btnAlimentarManual.setOnClickListener(v -> confirmarAlimentacionManual());
        findViewById(R.id.btnGuardarHorario).setOnClickListener(v -> guardarHorarioAutomatico());
    }

    @Override
    protected void onStart() {
        super.onStart();
        // Activa la escucha en tiempo real mientras la pantalla está visible
        refNivelComida.addValueEventListener(listenerNivel);
        consultaHistorial.addValueEventListener(listenerHistorial);
        refHorarioFijo.addValueEventListener(listenerHorario);
        refActivarManual.addValueEventListener(listenerManual);
    }

    @Override
    protected void onStop() {
        super.onStop();
        // Libera los listeners para no consumir datos en segundo plano
        refNivelComida.removeEventListener(listenerNivel);
        consultaHistorial.removeEventListener(listenerHistorial);
        refHorarioFijo.removeEventListener(listenerHorario);
        refActivarManual.removeEventListener(listenerManual);
    }

    // =====================================================================
    // INICIALIZACIÓN
    // =====================================================================

    private void enlazarVistas() {
        tvNivelComida = findViewById(R.id.tvNivelComida);
        tvEstadoNivel = findViewById(R.id.tvEstadoNivel);
        pbNivelComida = findViewById(R.id.pbNivelComida);
        btnAlimentarManual = findViewById(R.id.btnAlimentarManual);
        timePickerHorario = findViewById(R.id.timePickerHorario);
        tvHorarioActual = findViewById(R.id.tvHorarioActual);
        tvTotalHistorial = findViewById(R.id.tvTotalHistorial);

        timePickerHorario.setIs24HourView(true);

        ListView lvHistorial = findViewById(R.id.lvHistorial);
        adaptadorHistorial = new HistorialAdapter(this, registrosHistorial);
        lvHistorial.setAdapter(adaptadorHistorial);
        lvHistorial.setEmptyView(findViewById(R.id.tvHistorialVacio));
    }

    private void inicializarFirebase() {
        FirebaseDatabase database = DATABASE_URL.isEmpty()
                ? FirebaseDatabase.getInstance()
                : FirebaseDatabase.getInstance(DATABASE_URL);

        refActivarManual = database.getReference(RUTA_ACTIVAR_MANUAL);
        refHorarioFijo = database.getReference(RUTA_HORARIO_FIJO);
        refNivelComida = database.getReference(RUTA_NIVEL_COMIDA);
        // Las llaves push() son cronológicas: se piden las últimas N
        consultaHistorial = database.getReference(RUTA_HISTORIAL)
                .orderByKey()
                .limitToLast(MAX_REGISTROS_HISTORIAL);
    }

    private void crearListeners() {
        // /monitoreo/nivelComida -> indicador del depósito
        listenerNivel = new ValueEventListener() {
            @Override
            public void onDataChange(@NonNull DataSnapshot snapshot) {
                mostrarNivel(convertirPorcentaje(snapshot.getValue()));
            }

            @Override
            public void onCancelled(@NonNull DatabaseError error) {
                mostrarError(error);
            }
        };

        // /historial_comidas -> lista ordenada del más reciente al más antiguo
        listenerHistorial = new ValueEventListener() {
            @Override
            public void onDataChange(@NonNull DataSnapshot snapshot) {
                registrosHistorial.clear();
                for (DataSnapshot registro : snapshot.getChildren()) {
                    Object valor = registro.getValue();
                    if (valor != null) {
                        registrosHistorial.add(String.valueOf(valor).trim());
                    }
                }
                // "AAAA-MM-DD HH:MM:SS" ordena bien como texto: orden inverso = más reciente primero
                Collections.sort(registrosHistorial, Collections.reverseOrder());
                adaptadorHistorial.notifyDataSetChanged();
                tvTotalHistorial.setText(registrosHistorial.isEmpty()
                        ? ""
                        : getString(R.string.historial_total_formato, registrosHistorial.size()));
            }

            @Override
            public void onCancelled(@NonNull DatabaseError error) {
                mostrarError(error);
            }
        };

        // /control/horarioFijo -> muestra el horario guardado (sincronizado entre dispositivos)
        listenerHorario = new ValueEventListener() {
            @Override
            public void onDataChange(@NonNull DataSnapshot snapshot) {
                String horario = snapshot.getValue() == null ? "" : String.valueOf(snapshot.getValue());
                if (horario.matches("^([01]\\d|2[0-3]):[0-5]\\d$")) {
                    tvHorarioActual.setText(getString(R.string.horario_actual_formato, horario));
                    if (!horarioCargadoEnPicker) {
                        timePickerHorario.setHour(Integer.parseInt(horario.substring(0, 2)));
                        timePickerHorario.setMinute(Integer.parseInt(horario.substring(3, 5)));
                        horarioCargadoEnPicker = true;
                    }
                } else {
                    tvHorarioActual.setText(R.string.horario_sin_programar);
                }
            }

            @Override
            public void onCancelled(@NonNull DatabaseError error) {
                mostrarError(error);
            }
        };

        // /control/activarManual -> muestra si hay una orden en curso
        listenerManual = new ValueEventListener() {
            @Override
            public void onDataChange(@NonNull DataSnapshot snapshot) {
                // Acepta booleano true o el texto "true" (por si se editó a mano en la consola)
                Object valor = snapshot.getValue();
                boolean pendiente = Boolean.TRUE.equals(valor) || "true".equals(String.valueOf(valor));
                btnAlimentarManual.setText(pendiente
                        ? R.string.boton_manual_pendiente
                        : R.string.boton_manual);
            }

            @Override
            public void onCancelled(@NonNull DatabaseError error) {
                mostrarError(error);
            }
        };
    }

    // =====================================================================
    // ACCIONES DEL USUARIO
    // =====================================================================

    private void confirmarAlimentacionManual() {
        new AlertDialog.Builder(this)
                .setTitle(R.string.dialogo_manual_titulo)
                .setMessage(R.string.dialogo_manual_mensaje)
                .setPositiveButton(R.string.dialogo_si, (dialogo, which) -> activarAlimentacionManual())
                .setNegativeButton(R.string.dialogo_no, null)
                .show();
    }

    // Escribe true en /control/activarManual; la ESP32 dispensa y lo vuelve a false
    private void activarAlimentacionManual() {
        refActivarManual.setValue(true)
                .addOnSuccessListener(unused ->
                        Toast.makeText(this, R.string.toast_manual_ok, Toast.LENGTH_SHORT).show())
                .addOnFailureListener(e ->
                        Toast.makeText(this, getString(R.string.toast_error_formato, e.getMessage()),
                                Toast.LENGTH_LONG).show());
    }

    // Lee el TimePicker y guarda "HH:MM" con dos dígitos en /control/horarioFijo
    private void guardarHorarioAutomatico() {
        int hora = timePickerHorario.getHour();
        int minuto = timePickerHorario.getMinute();
        String horario = String.format(Locale.US, "%02d:%02d", hora, minuto);

        refHorarioFijo.setValue(horario)
                .addOnSuccessListener(unused ->
                        Toast.makeText(this, getString(R.string.toast_horario_ok, horario),
                                Toast.LENGTH_SHORT).show())
                .addOnFailureListener(e ->
                        Toast.makeText(this, getString(R.string.toast_error_formato, e.getMessage()),
                                Toast.LENGTH_LONG).show());
    }

    // =====================================================================
    // UTILIDADES DE INTERFAZ
    // =====================================================================

    // Acepta número (Long/Double) o texto ("75" o "75%"). Devuelve -1 si no es válido.
    private int convertirPorcentaje(Object valor) {
        if (valor == null) {
            return -1;
        }
        int porcentaje;
        if (valor instanceof Number) {
            porcentaje = (int) Math.round(((Number) valor).doubleValue());
        } else {
            try {
                porcentaje = (int) Math.round(Double.parseDouble(
                        String.valueOf(valor).replace("%", "").trim()));
            } catch (NumberFormatException e) {
                return -1;
            }
        }
        return Math.max(0, Math.min(100, porcentaje));
    }

    private void mostrarNivel(int porcentaje) {
        if (porcentaje < 0) {
            tvNivelComida.setText(R.string.nivel_sin_datos);
            tvEstadoNivel.setText(R.string.estado_esperando);
            pbNivelComida.setProgressCompat(0, true);
            return;
        }

        int color;
        int estado;
        if (porcentaje >= 50) {
            color = ContextCompat.getColor(this, R.color.nivel_alto);
            estado = R.string.estado_alto;
        } else if (porcentaje >= 20) {
            color = ContextCompat.getColor(this, R.color.nivel_medio);
            estado = R.string.estado_medio;
        } else {
            color = ContextCompat.getColor(this, R.color.nivel_bajo);
            estado = R.string.estado_bajo;
        }

        tvNivelComida.setText(getString(R.string.nivel_formato, porcentaje));
        tvNivelComida.setTextColor(color);
        tvEstadoNivel.setText(estado);
        tvEstadoNivel.setTextColor(color);
        pbNivelComida.setIndicatorColor(color);
        pbNivelComida.setProgressCompat(porcentaje, true);
    }

    private void mostrarError(DatabaseError error) {
        Log.e(TAG, "Firebase: " + error.getMessage());
        Toast.makeText(this, getString(R.string.toast_error_formato, error.getMessage()),
                Toast.LENGTH_LONG).show();
    }
}
