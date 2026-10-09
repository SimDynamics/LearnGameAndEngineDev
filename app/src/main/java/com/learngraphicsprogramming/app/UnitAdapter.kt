package com.learngraphicsprogramming.app

import android.content.res.ColorStateList
import android.graphics.Color
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import com.learngraphicsprogramming.app.databinding.ItemUnitBinding

class UnitAdapter(

    private val courseId: String,

    private val units: List<LearningUnit>,

    private val routeId: String =
        AppearanceSettingsRepository.ROUTE_PROGRAMMING,

    private val onUnitClick: (LearningUnit) -> Unit

) : RecyclerView.Adapter<UnitAdapter.UnitViewHolder>() {

    class UnitViewHolder(
        val binding: ItemUnitBinding
    ) : RecyclerView.ViewHolder(
        binding.root
    )

    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): UnitViewHolder {

        val binding =
            ItemUnitBinding.inflate(
                LayoutInflater.from(
                    parent.context
                ),
                parent,
                false
            )

        return UnitViewHolder(
            binding
        )
    }

    override fun onBindViewHolder(
        holder: UnitViewHolder,
        position: Int
    ) {

        val unit =
            units[position]

        val context =
            holder.itemView.context

        val accentColor =
            AppearanceSettingsRepository
                .getRouteAccentColor(
                    context,
                    routeId
                )

        val accentTextColor =
            AppearanceSettingsRepository
                .getRouteAccentTextColor(
                    context,
                    routeId
                )

        /*
         * LECCIONES COMPLETADAS.
         *
         * Se utiliza para el texto:
         *
         * 1 de 3 lecciones completadas
         *
         * y para determinar si toda
         * la unidad está terminada.
         */
        val completionProgress =
            ProgressRepository
                .getUnitProgress(
                    context,
                    routeId,
                    courseId,
                    unit.id
                )

        /*
         * PROGRESO REAL ACUMULADO.
         *
         * Incluye también el porcentaje
         * de las lecciones parcialmente
         * avanzadas.
         *
         * Ejemplo:
         *
         * 100 % + 85 % + 0 %
         * = 185 / 300
         * = 61 %
         */
        val learningProgress =
            ProgressRepository
                .getUnitLearningProgress(
                    context,
                    routeId,
                    courseId,
                    unit.id
                )

        holder.binding.textUnitNumber.text =
            context.getString(
                R.string.nav_unit_number,
                unit.number
            )

        holder.binding.textUnitTitle.text =
            unit.title

        /*
         * El contador sigue expresando
         * LECCIONES realmente terminadas.
         *
         * El texto se adapta al idioma
         * seleccionado en Opciones globales.
         */
        holder.binding.textUnitProgress.text =
            context.resources.getQuantityString(
                R.plurals.lesson_completed_lessons,
                completionProgress.total,
                completionProgress.completed,
                completionProgress.total
            )

        /*
         * El porcentaje visible ahora
         * representa TODO el avance real
         * acumulado dentro de la unidad.
         */
        holder.binding.textUnitPercentage.text =
            context.getString(R.string.lesson_percentage, learningProgress.percentage)

        holder.binding.progressUnit.progress =
            learningProgress.percentage

        holder.binding.progressUnit.progressTintList =
            ColorStateList.valueOf(
                accentColor
            )

        /*
         * La tarjeta solo se pinta como
         * completamente terminada cuando
         * TODAS sus lecciones están al 100 %.
         */
        if (
            completionProgress.total > 0 &&
            completionProgress.completed ==
            completionProgress.total
        ) {

            holder.binding.unitContainer
                .setBackgroundColor(
                    Color.rgb(
                        29,
                        78,
                        52
                    )
                )

            holder.binding.textUnitNumber
                .setTextColor(
                    Color.rgb(
                        105,
                        220,
                        145
                    )
                )

        } else {

            holder.binding.unitContainer
                .setBackgroundColor(
                    Color.rgb(
                        32,
                        33,
                        38
                    )
                )

            holder.binding.textUnitNumber
                .setTextColor(
                    accentTextColor
                )
        }

        holder.binding.unitContainer
            .setOnClickListener {

                onUnitClick(
                    unit
                )
            }
    }

    override fun getItemCount(): Int {

        return units.size
    }
}
