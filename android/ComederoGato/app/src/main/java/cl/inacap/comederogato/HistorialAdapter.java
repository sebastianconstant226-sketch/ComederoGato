package cl.inacap.comederogato;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.List;

/**
 * Adaptador personalizado para mostrar cada comida del historial.
 * Recibe textos con formato "AAAA-MM-DD HH:MM:SS" y los muestra como
 * hora grande + fecha "DD/MM/AAAA".
 */
public class HistorialAdapter extends ArrayAdapter<String> {

    private final LayoutInflater inflater;

    public HistorialAdapter(@NonNull Context context, @NonNull List<String> registros) {
        super(context, R.layout.item_historial, registros);
        this.inflater = LayoutInflater.from(context);
    }

    @NonNull
    @Override
    public View getView(int position, @Nullable View convertView, @NonNull ViewGroup parent) {
        View vista = convertView;
        ViewHolder holder;

        if (vista == null) {
            vista = inflater.inflate(R.layout.item_historial, parent, false);
            holder = new ViewHolder();
            holder.tvHora = vista.findViewById(R.id.tvItemHora);
            holder.tvFecha = vista.findViewById(R.id.tvItemFecha);
            holder.tvEtiqueta = vista.findViewById(R.id.tvItemEtiqueta);
            vista.setTag(holder);
        } else {
            holder = (ViewHolder) vista.getTag();
        }

        String registro = getItem(position);
        if (registro == null) {
            registro = "";
        }

        // "2026-10-28 08:05:00" -> hora "08:05:00", fecha "28/10/2026"
        String[] partes = registro.trim().split(" ");
        if (partes.length == 2) {
            String[] fecha = partes[0].split("-");
            holder.tvHora.setText(partes[1]);
            if (fecha.length == 3) {
                holder.tvFecha.setText(fecha[2] + "/" + fecha[1] + "/" + fecha[0]);
            } else {
                holder.tvFecha.setText(partes[0]);
            }
        } else {
            holder.tvHora.setText(registro);
            holder.tvFecha.setText("");
        }

        // La primera fila (la más reciente) lleva la etiqueta "ÚLTIMA"
        holder.tvEtiqueta.setVisibility(position == 0 ? View.VISIBLE : View.GONE);

        return vista;
    }

    private static class ViewHolder {
        TextView tvHora;
        TextView tvFecha;
        TextView tvEtiqueta;
    }
}
