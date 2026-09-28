package com.learngraphicsprogramming.app

import android.content.res.ColorStateList
import android.graphics.Color
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.recyclerview.widget.RecyclerView
import com.learngraphicsprogramming.app.databinding.ItemCourseBinding

class CourseAdapter(

    private val courses: List<Course>,

    private val onCourseClick: (Course) -> Unit

) : RecyclerView.Adapter<CourseAdapter.CourseViewHolder>() {

    class CourseViewHolder(
        val binding: ItemCourseBinding
    ) : RecyclerView.ViewHolder(
        binding.root
    )

    override fun onCreateViewHolder(
        parent: ViewGroup,
        viewType: Int
    ): CourseViewHolder {

        val binding =
            ItemCourseBinding.inflate(
                LayoutInflater.from(
                    parent.context
                ),
                parent,
                false
            )

        return CourseViewHolder(
            binding
        )
    }

    override fun onBindViewHolder(
        holder: CourseViewHolder,
        position: Int
    ) {

        val course =
            courses[position]

        val context =
            holder.itemView.context

        val accentColor =
            AppearanceSettingsRepository
                .getAccentColor(
                    context
                )

        val accentTextColor =
            AppearanceSettingsRepository
                .getAccentTextColor(
                    context
                )

        val progress =
            ProgressRepository
                .getCourseProgress(
                    context,
                    course.id
                )

        holder.binding.progressCourse.progressTintList =
            ColorStateList.valueOf(
                accentColor
            )

        holder.binding.textCourseNumber.text =
            "Curso ${course.number} de ${courses.size}"

        holder.binding.textCourseTitle.text =
            course.title

        /*
         * Los cursos que todavía no tienen
         * contenido real mostrarán su estado
         * de desarrollo en lugar de 0 de 0.
         */
        if (
            progress.total == 0
        ) {

            holder.binding.textCourseProgress.text =
                "En desarrollo · Próximamente disponible"

            holder.binding.textCoursePercentage.text =
                ""

            holder.binding.progressCourse.progress =
                0

        } else {

            holder.binding.textCourseProgress.text =
                "${progress.completed} de ${progress.total} lecciones completadas"

            holder.binding.textCoursePercentage.text =
                "${progress.percentage} %"

            holder.binding.progressCourse.progress =
                progress.percentage
        }

        /*
         * Curso completamente terminado.
         */
        if (
            progress.total > 0 &&
            progress.completed ==
            progress.total
        ) {

            holder.binding.courseContainer
                .setBackgroundColor(
                    Color.rgb(
                        29,
                        78,
                        52
                    )
                )

            holder.binding.textCourseNumber
                .setTextColor(
                    Color.rgb(
                        105,
                        220,
                        145
                    )
                )

        } else {

            holder.binding.courseContainer
                .setBackgroundColor(
                    Color.rgb(
                        32,
                        33,
                        38
                    )
                )

            holder.binding.textCourseNumber
                .setTextColor(
                    accentTextColor
                )
        }

        holder.binding.courseContainer
            .setOnClickListener {

                onCourseClick(
                    course
                )
            }
    }

    override fun getItemCount(): Int {

        return courses.size
    }
}